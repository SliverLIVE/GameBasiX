// GameBasiX - Hintergrund-Jobs
#include "jobs.h"

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "interpreter.h"

namespace jobs {

#if defined(__EMSCRIPTEN__)

// Im Browser gibt es keine Threads. Die Jobs werden deshalb sofort erledigt
// und als bereits fertig abgelegt; die BASIC-Seite sieht dieselbe Schnittstelle.
namespace {

struct Job {
    Value value;
    std::string error;
    bool failed = false;
};

std::unordered_map<int, Job> g_jobs;
int g_nextId = 1;
constexpr size_t kMaxJobs = 32;

}  // namespace

bool available() { return true; }

int submit(std::function<Value()> work) {
    if (g_jobs.size() >= kMaxJobs) return -1;
    Job job;
    try {
        job.value = cloneValue(work());
    } catch (const BasicError& e) {
        job.error = e.fullMessage();
        job.failed = true;
    } catch (const std::exception& e) {
        job.error = e.what();
        job.failed = true;
    } catch (...) {
        job.error = "unknown error in job";
        job.failed = true;
    }
    const int id = g_nextId++;
    g_jobs.emplace(id, std::move(job));
    return id;
}

bool done(int id) { return g_jobs.find(id) != g_jobs.end(); }

Value value(int id, std::string& error) {
    error.clear();
    auto found = g_jobs.find(id);
    if (found == g_jobs.end()) {
        error = "Unknown job: " + std::to_string(id);
        return Value::mkNull();
    }
    if (found->second.failed) {
        error = found->second.error;
        return Value::mkNull();
    }
    return cloneValue(found->second.value);
}

std::string error(int id) {
    auto found = g_jobs.find(id);
    return found == g_jobs.end() ? "Unknown job: " + std::to_string(id) : found->second.error;
}

bool release(int id) { return g_jobs.erase(id) > 0; }
void shutdown() { g_jobs.clear(); }

int spawnFunction(Program& prog, const std::string& name, std::vector<Value> args,
                  const std::unordered_map<std::string, Slot>& globals, const SrcPos& pos) {
    auto found = prog.funcs.find(name);
    if (found == prog.funcs.end())
        throw BasicError("Unknown function: " + name, pos, false);
    if (found->second.isMethod())
        throw BasicError("SPAWN cannot call a METHOD: " + name, pos, false);
    if (found->second.isSub)
        throw BasicError("SPAWN needs a FUNCTION, not a SUB: " + name, pos, false);
    for (const Param& param : found->second.params) {
        if (param.byref)
            throw BasicError("SPAWN cannot use BYREF parameters: " + name, pos, false);
    }

    std::unordered_map<std::string, Slot> snapshot;
    snapshot.reserve(globals.size());
    for (const auto& entry : globals) {
        Slot slot = entry.second;
        slot.v = cloneValue(slot.v);
        snapshot.emplace(entry.first, std::move(slot));
    }
    for (Value& arg : args) arg = cloneValue(arg);

    Program* program = &prog;
    const int id = submit([program, name, args = std::move(args),
                           snapshot = std::move(snapshot), pos]() mutable {
        Runtime workerRt;
        workerRt.headless = true;
        workerRt.worker = true;
        workerRt.prog = program;
        workerRt.globals = std::move(snapshot);
        workerRt.strict = true;

        Interpreter worker(*program, workerRt);
        workerRt.interp = &worker;
        auto def = program->funcs.find(name);
        if (def == program->funcs.end())
            throw BasicError("Unknown function: " + name, pos, false);
        return worker.callUserFunctionValues(def->second, std::move(args), pos);
    });
    if (id < 0) throw BasicError("Too many background jobs", pos, false);
    return id;
}

#else

bool available() { return true; }

namespace {

struct Job {
    std::thread thread;
    std::atomic<bool> done{false};
    std::mutex mutex;
    Value value;
    std::string error;
    bool failed = false;
};

std::mutex g_mutex;
std::unordered_map<int, std::shared_ptr<Job>> g_jobs;
int g_nextId = 1;
constexpr size_t kMaxJobs = 32;

}  // namespace

int submit(std::function<Value()> work) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_jobs.size() >= kMaxJobs) return -1;

    auto job = std::make_shared<Job>();
    const int id = g_nextId++;
    job->thread = std::thread([job, work = std::move(work)]() mutable {
        try {
            Value result = work();
            std::lock_guard<std::mutex> guard(job->mutex);
            job->value = cloneValue(result);
        } catch (const BasicError& e) {
            std::lock_guard<std::mutex> guard(job->mutex);
            job->error = e.fullMessage();
            job->failed = true;
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> guard(job->mutex);
            job->error = e.what();
            job->failed = true;
        } catch (...) {
            std::lock_guard<std::mutex> guard(job->mutex);
            job->error = "unknown error in job";
            job->failed = true;
        }
        job->done.store(true, std::memory_order_release);
    });
    g_jobs.emplace(id, std::move(job));
    return id;
}

bool done(int id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_jobs.find(id);
    if (found == g_jobs.end()) return false;
    return found->second->done.load(std::memory_order_acquire);
}

Value value(int id, std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_jobs.find(id);
    if (found == g_jobs.end()) {
        error = "Unknown job: " + std::to_string(id);
        return Value::mkNull();
    }
    const std::shared_ptr<Job>& job = found->second;
    if (!job->done.load(std::memory_order_acquire)) {
        error = "Job has not finished yet: " + std::to_string(id);
        return Value::mkNull();
    }
    std::lock_guard<std::mutex> guard(job->mutex);
    if (job->failed) {
        error = job->error;
        return Value::mkNull();
    }
    return cloneValue(job->value);
}

std::string error(int id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_jobs.find(id);
    if (found == g_jobs.end()) return "Unknown job: " + std::to_string(id);
    std::lock_guard<std::mutex> guard(found->second->mutex);
    return found->second->error;
}

bool release(int id) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto found = g_jobs.find(id);
        if (found == g_jobs.end()) return false;
        job = std::move(found->second);
        g_jobs.erase(found);
    }
    if (job->thread.joinable()) job->thread.join();
    return true;
}

void shutdown() {
    std::unordered_map<int, std::shared_ptr<Job>> all;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        all.swap(g_jobs);
    }
    for (auto& entry : all) {
        if (entry.second->thread.joinable()) entry.second->thread.join();
    }
}

int spawnFunction(Program& prog, const std::string& name, std::vector<Value> args,
                  const std::unordered_map<std::string, Slot>& globals, const SrcPos& pos) {
    auto found = prog.funcs.find(name);
    if (found == prog.funcs.end())
        throw BasicError("Unknown function: " + name, pos, false);
    if (found->second.isMethod())
        throw BasicError("SPAWN cannot call a METHOD: " + name, pos, false);
    if (found->second.isSub)
        throw BasicError("SPAWN needs a FUNCTION, not a SUB: " + name, pos, false);
    for (const Param& param : found->second.params) {
        if (param.byref)
            throw BasicError("SPAWN cannot use BYREF parameters: " + name, pos, false);
    }

    std::unordered_map<std::string, Slot> snapshot;
    snapshot.reserve(globals.size());
    for (const auto& entry : globals) {
        Slot slot = entry.second;
        slot.v = cloneValue(slot.v);
        snapshot.emplace(entry.first, std::move(slot));
    }
    for (Value& arg : args) arg = cloneValue(arg);

    Program* program = &prog;
    const int id = submit([program, name, args = std::move(args),
                           snapshot = std::move(snapshot), pos]() mutable {
        Runtime workerRt;
        workerRt.headless = true;
        workerRt.worker = true;
        workerRt.prog = program;
        workerRt.globals = std::move(snapshot);
        workerRt.strict = true;

        Interpreter worker(*program, workerRt);
        workerRt.interp = &worker;
        auto def = program->funcs.find(name);
        if (def == program->funcs.end())
            throw BasicError("Unknown function: " + name, pos, false);
        return worker.callUserFunctionValues(def->second, std::move(args), pos);
    });
    if (id < 0) throw BasicError("Too many background jobs", pos, false);
    return id;
}

#endif

}  // namespace jobs
