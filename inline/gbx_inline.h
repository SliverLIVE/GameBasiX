/* GameBasiX - INLINE interface
 *
 * C++ code inside an INLINE … ENDINLINE block is compiled when the program
 * starts and loaded as a DLL/SO. The block reports what it offers through
 * GBX_INLINE_FN(...); afterwards each of these functions is an ordinary
 * GBX command or function.
 *
 * Example:
 *
 *     INLINE
 *     #include <gbx_inline.h>
 *
 *     static double twice(const GbxValue* args, int argc, GbxValue* ret) {
 *         gbxinline::retNum(ret, gbxinline::argNum(args, argc, 0) * 2);
 *     }
 *     GBX_INLINE_FN("TWICE", 1, 1, twice);
 *     ENDINLINE
 *
 *     PRINT TWICE(21) + "", 10, 10
 *
 * Values: numbers (INTEGER, FLOAT) arrive as double, text as const char*
 * (UTF-8, valid only during the call). Arrays and TYPEs cannot be passed.
 * Return values: gbxinline::retNum(ret, value) for numbers,
 * gbxinline::retText(ret, text) for strings.
 *
 * A step block (INLINE in the middle of the code) also sees the BASIC
 * variables and may call the built-in GBX commands, e.g.
 * PRINT(text_str, x, y) or DRAW_LINE(...). See gbxinline::call().
 */
#ifndef GBX_INLINE_H
#define GBX_INLINE_H

#define GBX_INLINE_VERSION 2

/* Entry points of a library built from INLINE blocks. The interpreter writes
 * GBX_INLINE_EXPORT in front of them, so the same text compiles both as a
 * DLL/SO and as part of the HTML5 player. */
#if defined(_WIN32)
#define GBX_INLINE_EXPORT extern "C" __declspec(dllexport)
#else
#define GBX_INLINE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/* A value between BASIC and C++. */
typedef struct GbxValue {
    int type;          /* 0 = number, 1 = text */
    double num;        /* valid when type is 0 */
    const char* text;  /* valid when type is 1 */
} GbxValue;

typedef void (*GbxInlineFn)(const GbxValue* args, int argc, GbxValue* ret);

/* One item for array operations: either a reference to an existing BASIC
 * element (ref) or a fresh number/text value.
 * type: 0 = number (num), 1 = text (text); ref takes precedence. item == NULL
 * means "default value of the array" (0, "" or a new TYPE). */
typedef struct GbxElem {
    void* ref;
    int type;
    double num;
    const char* text;
} GbxElem;

/* What the interpreter offers a step block: access to the BASIC variables
 * at that point. A step block is an INLINE block in the middle of the code;
 * it is called at exactly that place while running. */
typedef struct GbxStep {
    int version;
    void* scope;
    void* (*variable)(void* scope, const char* name); /* BASIC variable */
    void* (*field)(void* value, const char* name);    /* field of a TYPE */
    void* (*item)(void* value, int index);            /* array element */
    int (*count)(void* value);                        /* array: item count */
    int (*value_type)(void* value);                   /* 0 number, 1 text, 2 array, 3 TYPE, -1 empty */
    double (*get_num)(void* value);
    void (*set_num)(void* value, double value2);
    const char* (*get_text)(void* value);
    void (*set_text)(void* value, const char* text);
    void (*log)(const char* text);
    void (*fail)(void* scope, const char* message);

    /* Changing arrays: 1 = success, 0 = error (already set as a runtime error).
     * Fields are only appended, so older libraries keep working. */
    int (*rank)(void* value);                  /* number of dimensions */
    int (*size_dim)(void* value, int dim);     /* size of one dimension */
    int (*resize)(void* value, const int* sizes, int dims); /* REDIM */
    int (*push)(void* value, const GbxElem* item);
    int (*insert)(void* value, int index, const GbxElem* item);
    int (*remove)(void* value, int index);
    int (*clear)(void* value);

    /* Call built-in GBX commands from a step (PRINT, DRAW_LINE …).
     * args[i].ref passes a variable as a BYREF target, otherwise the
     * number/text value is used. ret is 0 = number, 1 = text (text stays valid
     * until the next call). Returns 1 = ok, 0 = error (already set as a runtime
     * error). */
    int (*basic_call)(void* scope, const char* name, const GbxElem* args, int argc, GbxValue* ret);
} GbxStep;

/* What the interpreter offers the block. */
typedef struct GbxHost {
    int version;
    void (*register_function)(struct GbxHost* host, const char* name, int minArgs, int maxArgs,
                              GbxInlineFn fn);
    void (*set_string)(struct GbxHost* host, GbxValue* ret, const char* text);
    void (*log)(struct GbxHost* host, const char* text);
    void* user;
} GbxHost;

#ifdef __cplusplus
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

namespace gbxinline {

constexpr int kNumber = 0;
constexpr int kText = 1;

struct Registration {
    const char* name;
    int minArgs;
    int maxArgs;
    GbxInlineFn fn;
};

/** All registrations of the block; the interpreter fetches them on load. */
inline std::vector<Registration>& registrations() {
    static std::vector<Registration> list;
    return list;
}

inline GbxHost*& hostPointer() {
    static GbxHost* host = nullptr;
    return host;
}

struct Registrar {
    Registrar(const char* name, int minArgs, int maxArgs, GbxInlineFn fn) {
        if (name && fn) registrations().push_back(Registration{name, minArgs, maxArgs, fn});
    }
};

/** Called by the interpreter: register all functions of this block. */
inline void registerAll(GbxHost* host) {
    hostPointer() = host;
    if (!host) return;
    for (const Registration& registration : registrations()) {
        host->register_function(host, registration.name, registration.minArgs, registration.maxArgs,
                                registration.fn);
    }
}

/* ------------------------------------------------------------ Arguments */

inline double argNum(const GbxValue* args, int argc, int index, double fallback = 0.0) {
    return (index >= 0 && index < argc && args[index].type == kNumber) ? args[index].num : fallback;
}

inline bool argIsText(const GbxValue* args, int argc, int index) {
    return index >= 0 && index < argc && args[index].type == kText;
}

inline std::string argText(const GbxValue* args, int argc, int index) {
    if (!argIsText(args, argc, index) || !args[index].text) return std::string();
    return std::string(args[index].text);
}

/* ------------------------------------------------------------- Results */

inline void retNum(GbxValue* ret, double value) {
    if (!ret) return;
    ret->type = kNumber;
    ret->num = value;
    ret->text = nullptr;
}

inline void retText(GbxValue* ret, const std::string& value) {
    if (!ret) return;
    ret->type = kText;
    ret->num = 0.0;
    ret->text = nullptr;
    if (GbxHost* host = hostPointer()) host->set_string(host, ret, value.c_str());
}

inline void retText(GbxValue* ret, const char* value) {
    retText(ret, std::string(value ? value : ""));
}

/** Writes a line to the IDE console or to stdout. */
inline void log(const std::string& text) {
    if (GbxHost* host = hostPointer()) host->log(host, text.c_str());
}

/* ------------------------------------------------ Steps in BASIC code
 *
 * An INLINE block in the middle of the code runs in place. The BASIC
 * variables appear there as C++ names: `counter` stays `counter`, a text
 * `name` becomes `name_str`, an array `list` becomes `list[i]`, and a
 * TYPE `p` exposes its fields as `p.x`. Names are lower case in C++;
 * BASIC names are case-insensitive anyway.
 */

inline GbxStep*& stepHost() {
    static GbxStep* host = nullptr;
    return host;
}

inline void enterStep(GbxStep* host) { stepHost() = host; }

inline void* variable(const char* name) {
    GbxStep* host = stepHost();
    return host && host->variable ? host->variable(host->scope, name) : nullptr;
}

inline void* field(void* value, const char* name) {
    GbxStep* host = stepHost();
    return host && host->field ? host->field(value, name) : nullptr;
}

inline void* item(void* value, int index) {
    GbxStep* host = stepHost();
    return host && host->item ? host->item(value, index) : nullptr;
}

inline int count(void* value) {
    GbxStep* host = stepHost();
    return host && host->count ? host->count(value) : 0;
}

inline int valueType(void* value) {
    GbxStep* host = stepHost();
    return host && host->value_type ? host->value_type(value) : -1;
}

inline double readNum(void* value) {
    GbxStep* host = stepHost();
    return host && host->get_num ? host->get_num(value) : 0.0;
}

inline void writeNum(void* value, double number) {
    GbxStep* host = stepHost();
    if (host && host->set_num) host->set_num(value, number);
}

inline const char* readText(void* value) {
    GbxStep* host = stepHost();
    const char* text = host && host->get_text ? host->get_text(value) : nullptr;
    return text ? text : "";
}

inline void writeText(void* value, const char* text) {
    GbxStep* host = stepHost();
    if (host && host->set_text) host->set_text(value, text ? text : "");
}

/** Reports an error to the interpreter; the step stops afterwards. */
inline void fail(const char* message) {
    GbxStep* host = stepHost();
    if (host && host->fail) host->fail(host->scope, message ? message : "Error in INLINE block");
}

/* ------------------------------------------------------------ Arrays */

/** Number of dimensions of a BASIC array (1 … 4). */
inline int arrayRank(void* value) {
    GbxStep* host = stepHost();
    return host && host->rank ? host->rank(value) : 0;
}

/** Size of one dimension; dim 0 = first dimension (like ARRAY_BOUNDS). */
inline int arraySize(void* value, int dim) {
    GbxStep* host = stepHost();
    return host && host->size_dim ? host->size_dim(value, dim) : 0;
}

/** REDIM: new sizes; existing values stay as far as the shape allows. */
inline bool arrayResize(void* value, const int* sizes, int dims) {
    GbxStep* host = stepHost();
    return host && host->resize ? host->resize(value, sizes, dims) != 0 : false;
}

inline bool arrayResize(void* value, int a) { return arrayResize(value, &a, 1); }
inline bool arrayResize(void* value, int a, int b) {
    const int sizes[2] = {a, b};
    return arrayResize(value, sizes, 2);
}
inline bool arrayResize(void* value, int a, int b, int c) {
    const int sizes[3] = {a, b, c};
    return arrayResize(value, sizes, 3);
}
inline bool arrayResize(void* value, int a, int b, int c, int d) {
    const int sizes[4] = {a, b, c, d};
    return arrayResize(value, sizes, 4);
}

/** Append an item; item.ref points to an existing element. */
inline bool arrayPush(void* value, const GbxElem& item) {
    GbxStep* host = stepHost();
    return host && host->push ? host->push(value, &item) != 0 : false;
}

/** Append a default value (0, "" or a new TYPE record). */
inline bool arrayPushDefault(void* value) {
    GbxStep* host = stepHost();
    return host && host->push ? host->push(value, nullptr) != 0 : false;
}

/** Append a copy of an existing element. */
inline bool arrayPushRef(void* value, void* reference) {
    GbxElem item{};
    item.ref = reference;
    return arrayPush(value, item);
}

/** Insert before index; a negative value counts from the end (-1 = append,
 *  -2 = before the last item - like GBXARRAY_INSERT). */
inline bool arrayInsert(void* value, int index, const GbxElem& item) {
    GbxStep* host = stepHost();
    return host && host->insert ? host->insert(value, index, &item) != 0 : false;
}

inline bool arrayInsertRef(void* value, int index, void* reference) {
    GbxElem item{};
    item.ref = reference;
    return arrayInsert(value, index, item);
}

inline bool arrayRemove(void* value, int index) {
    GbxStep* host = stepHost();
    return host && host->remove ? host->remove(value, index) != 0 : false;
}

inline bool arrayClear(void* value) {
    GbxStep* host = stepHost();
    return host && host->clear ? host->clear(value) != 0 : false;
}

class Array;

/** Number variable (or field): reads and writes like a double variable. */
class Num {
public:
    Num() = default;
    explicit Num(void* value) : value_(value) {}
    double value() const { return readNum(value_); }
    operator double() const { return value(); }
    /** Internal handle, e.g. as a BYREF argument for GBX commands. */
    void* raw() const { return value_; }
    Num& operator=(double number) { writeNum(value_, number); return *this; }
    Num& operator=(int number) { writeNum(value_, static_cast<double>(number)); return *this; }
    Num& operator=(const Num& other) { writeNum(value_, other.value()); return *this; }
    Num& operator+=(double number) { writeNum(value_, value() + number); return *this; }
    Num& operator-=(double number) { writeNum(value_, value() - number); return *this; }
    Num& operator*=(double number) { writeNum(value_, value() * number); return *this; }
    Num& operator/=(double number) { writeNum(value_, value() / number); return *this; }
    Num& operator++() { writeNum(value_, value() + 1.0); return *this; }
    double operator++(int) { const double old = value(); writeNum(value_, old + 1.0); return old; }
    Num& operator--() { writeNum(value_, value() - 1.0); return *this; }
    double operator--(int) { const double old = value(); writeNum(value_, old - 1.0); return old; }

private:
    void* value_ = nullptr;
};

/** Text variable: used like a std::string. */
class Str {
public:
    Str() = default;
    explicit Str(void* value) : value_(value) {}
    std::string value() const { return std::string(readText(value_)); }
    operator std::string() const { return value(); }
    Str& operator=(const std::string& text) { writeText(value_, text.c_str()); return *this; }
    Str& operator=(const char* text) { writeText(value_, text); return *this; }
    Str& operator=(const Str& other) { writeText(value_, other.c_str()); return *this; }
    Str& operator+=(const std::string& text) { writeText(value_, (value() + text).c_str()); return *this; }
    Str& operator+=(const char* text) { writeText(value_, (value() + (text ? text : "")).c_str()); return *this; }
    const char* c_str() const { return readText(value_); }
    std::size_t size() const { return value().size(); }
    bool empty() const { return value().empty(); }
    /** Internal handle, e.g. as a BYREF argument for GBX commands. */
    void* raw() const { return value_; }

private:
    void* value_ = nullptr;
};

inline std::string operator+(const Str& a, const std::string& b) { return a.value() + b; }
inline std::string operator+(const std::string& a, const Str& b) { return a + b.value(); }
inline std::string operator+(const Str& a, const char* b) { return a.value() + (b ? b : ""); }
inline std::string operator+(const char* a, const Str& b) { return std::string(a ? a : "") + b.value(); }
inline std::string operator+(const Str& a, const Str& b) { return a.value() + b.value(); }

/** An array element: number or text, also nested (a[i][j]). */
class Element {
public:
    Element() = default;
    explicit Element(void* value) : value_(value) {}
    double num() const { return readNum(value_); }
    std::string text() const { return std::string(readText(value_)); }
    operator double() const { return num(); }
    operator std::string() const { return text(); }
    /** Nested array (e.g. one row of field[i][j]). Only valid while the
     *  outer array is not resized or changed. */
    Array asArray() const;
    /** Internal handle for array operations. */
    void* raw() const { return value_; }
    Element& operator=(double number) { writeNum(value_, number); return *this; }
    Element& operator=(int number) { writeNum(value_, static_cast<double>(number)); return *this; }
    Element& operator=(const std::string& text) { writeText(value_, text.c_str()); return *this; }
    Element& operator=(const char* text) { writeText(value_, text); return *this; }
    Element& operator=(const Element& other) {
        if (other.value_ == value_) return *this;
        if (valueType(other.value_) == 1) {
            writeText(value_, readText(other.value_));
        } else {
            writeNum(value_, readNum(other.value_));
        }
        return *this;
    }
    Element operator[](int index) const { return Element(item(value_, index)); }
    Element field(const char* name) const { return Element(::gbxinline::field(value_, name)); }
    int count() const { return ::gbxinline::count(value_); }
    bool valid() const { return value_ != nullptr; }

private:
    void* value_ = nullptr;
};

/** Array variable: a[i] reads and writes; push/insert/remove/resize change
 *  the size - like GBXArray, but directly on the BASIC array. After
 *  resize/push/insert/remove, elements fetched earlier become invalid
 *  (the array may move in memory). */
class Array {
public:
    Array() = default;
    explicit Array(void* value) : value_(value) {}
    Element operator[](int index) const { return Element(item(value_, index)); }
    int size() const { return count(value_); }
    /** Size of one dimension; dim 0 = first (like ARRAY_BOUNDS). */
    int size(int dim) const { return arraySize(value_, dim); }
    int rank() const { return arrayRank(value_); }
    bool valid() const { return value_ != nullptr; }
    /** Internal handle, e.g. as a BYREF argument for GBX commands. */
    void* raw() const { return value_; }

    bool resize(int a) { return arrayResize(value_, a); }
    bool resize(int a, int b) { return arrayResize(value_, a, b); }
    bool resize(int a, int b, int c) { return arrayResize(value_, a, b, c); }
    bool resize(int a, int b, int c, int d) { return arrayResize(value_, a, b, c, d); }

    bool push(double number) {
        GbxElem entry{};
        entry.type = 0;
        entry.num = number;
        return arrayPush(value_, entry);
    }
    bool push(const char* text) {
        GbxElem entry{};
        entry.type = 1;
        entry.text = text ? text : "";
        return arrayPush(value_, entry);
    }
    bool push(const std::string& text) { return push(text.c_str()); }
    bool push(const Element& element) { return arrayPushRef(value_, element.raw()); }

    bool insert(int index, double number) {
        GbxElem entry{};
        entry.type = 0;
        entry.num = number;
        return arrayInsert(value_, index, entry);
    }
    bool insert(int index, const char* text) {
        GbxElem entry{};
        entry.type = 1;
        entry.text = text ? text : "";
        return arrayInsert(value_, index, entry);
    }
    bool insert(int index, const std::string& text) { return insert(index, text.c_str()); }
    bool insert(int index, const Element& element) {
        return arrayInsertRef(value_, index, element.raw());
    }

    bool remove(int index) { return arrayRemove(value_, index); }
    bool clear() { return arrayClear(value_); }
    Array& operator=(const Array&) = delete;

private:
    void* value_ = nullptr;
};

inline Array Element::asArray() const { return Array(value_); }

/** TYPE variable without generated field names (for special nested cases). */
class Record {
public:
    Record() = default;
    explicit Record(void* value) : value_(value) {}
    Element field(const char* name) const { return Element(::gbxinline::field(value_, name)); }
    bool valid() const { return value_ != nullptr; }
    Record& operator=(const Record&) = delete;

private:
    void* value_ = nullptr;
};

/* ------------------------------------------------- Calling GBX commands */

/** Result of a called GBX command: number or text. */
class Result {
public:
    Result() = default;
    explicit Result(const GbxValue& value)
        : number_(value.type == 1 ? 0.0 : value.num),
          text_(value.type == 1 && value.text ? value.text : ""),
          isText_(value.type == 1),
          ok_(true) {}

    bool ok() const { return ok_; }
    bool isText() const { return isText_; }
    double num() const { return isText_ ? std::strtod(text_.c_str(), nullptr) : number_; }
    std::string text() const { return isText_ ? text_ : numberText(number_); }
    operator double() const { return num(); }
    operator std::string() const { return text(); }

private:
    static std::string numberText(double value) {
        std::string out = std::to_string(value);
        const std::size_t dot = out.find('.');
        if (dot != std::string::npos) {
            while (!out.empty() && out.back() == '0') out.pop_back();
            if (!out.empty() && out.back() == '.') out.pop_back();
        }
        return out;
    }

    double number_ = 0.0;
    std::string text_;
    bool isText_ = false;
    bool ok_ = false;
};

namespace detail {

/** Collects the arguments of a GBX call. Texts live in the vector so that
 *  the pointers stay valid until the call. */
struct CallArgs {
    std::vector<GbxElem> items;
    std::vector<std::string> texts;

    void prepare(std::size_t count) {
        items.reserve(count);
        texts.reserve(count);
    }

    void number(double value) {
        GbxElem entry{};
        entry.type = 0;
        entry.num = value;
        items.push_back(entry);
    }
    void reference(void* value) {
        GbxElem entry{};
        entry.ref = value;
        items.push_back(entry);
    }
    void text(const char* value) {
        texts.emplace_back(value ? value : "");
        GbxElem entry{};
        entry.type = 1;
        entry.text = texts.back().c_str();
        items.push_back(entry);
    }

    void add(double value) { number(value); }
    void add(bool value) { number(value ? 1.0 : 0.0); }
    template <typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
    void add(T value) { number(static_cast<double>(value)); }

    void add(const char* value) { text(value); }
    void add(const std::string& value) { text(value.c_str()); }
    void add(const Num& value) { reference(value.raw()); }
    void add(const Str& value) { reference(value.raw()); }
    void add(const Element& value) { reference(value.raw()); }
    void add(const Array& value) { reference(value.raw()); }
    void add(const Result& value) {
        if (value.isText()) {
            text(value.text().c_str());
        } else {
            number(value.num());
        }
    }
};

}  // namespace detail

/** Calls a built-in GBX command directly from an INLINE step.
 *  Variables (Num, Str, Element, Array) are passed as BYREF targets, numbers
 *  and texts by value. Example: call("PRINT", text_str, 10, 10); */
template <typename... Args>
Result call(const char* name, const Args&... args) {
    detail::CallArgs list;
    list.prepare(sizeof...(Args));
    (list.add(args), ...);

    GbxValue ret{};
    ret.type = 0;
    GbxStep* host = stepHost();
    if (!host || !host->basic_call) {
        fail("GBX commands are only available in an INLINE step");
        return Result();
    }
    if (!host->basic_call(host->scope, name, list.items.empty() ? nullptr : list.items.data(),
                          static_cast<int>(list.items.size()), &ret)) {
        return Result();
    }
    return Result(ret);
}

/** RGB is not used as a function name because of the Windows macro. */
inline Result rgb(double r, double g, double b) { return call("RGB", r, g, b); }

/* Every built-in command without a $ in its name is available here as a
 * C++ function. Commands with $ (CHR$, LEFT$, STR$ …) are called through
 * call("CHR$", …). */
#define GBX_INLINE_COMMAND(name) \
    template <typename... Args> \
    inline ::gbxinline::Result name(Args&&... args) { return ::gbxinline::call(#name, args...); }

GBX_INLINE_COMMAND(ABS)
GBX_INLINE_COMMAND(ACOS)
GBX_INLINE_COMMAND(ALLOW_ESCAPE)
GBX_INLINE_COMMAND(SET_ALPHA_MODE)
GBX_INLINE_COMMAND(SET_ALPHA_TEST)
GBX_INLINE_COMMAND(ANIM_COLLIDE)
GBX_INLINE_COMMAND(ASC)
GBX_INLINE_COMMAND(ASIN)
GBX_INLINE_COMMAND(SHL)
GBX_INLINE_COMMAND(SHR)
GBX_INLINE_COMMAND(ASSERT)
GBX_INLINE_COMMAND(ATAN)
GBX_INLINE_COMMAND(BAND)
GBX_INLINE_COMMAND(SCREEN_BLACK)
GBX_INLINE_COMMAND(SCREEN_FADE)
GBX_INLINE_COMMAND(BNOT)
GBX_INLINE_COMMAND(BOR)
GBX_INLINE_COMMAND(ARRAY_BOUNDS)
GBX_INLINE_COMMAND(RECT_COLLIDE)
GBX_INLINE_COMMAND(BXOR)
GBX_INLINE_COMMAND(CEIL)
GBX_INLINE_COMMAND(SCREEN_CLEAR)
GBX_INLINE_COMMAND(SOURCE_LINE)
GBX_INLINE_COMMAND(COS)
GBX_INLINE_COMMAND(SCREEN_CREATE)
GBX_INLINE_COMMAND(DEBUG)
GBX_INLINE_COMMAND(DIMDATA)
GBX_INLINE_COMMAND(ANIM_DRAW)
GBX_INLINE_COMMAND(DRAW_RECT_BORDER)
GBX_INLINE_COMMAND(DRAW_CIRCLE)
GBX_INLINE_COMMAND(DRAW_CIRCLE_FILLED)
GBX_INLINE_COMMAND(DRAW_LINE)
GBX_INLINE_COMMAND(DRAW_RECT)
GBX_INLINE_COMMAND(SPRITE_DRAW)
GBX_INLINE_COMMAND(TEXT_DRAW)
GBX_INLINE_COMMAND(POLY_END)
GBX_INLINE_COMMAND(FLOOR)
GBX_INLINE_COMMAND(FMOD)
GBX_INLINE_COMMAND(FONT_NEW)
GBX_INLINE_COMMAND(SCREEN_NEW)
GBX_INLINE_COMMAND(SOUND_NEW)
GBX_INLINE_COMMAND(SPRITE_NEW)
GBX_INLINE_COMMAND(FONT_CURRENT)
GBX_INLINE_COMMAND(DESKTOP_SIZE)
GBX_INLINE_COMMAND(FONT_SIZE)
GBX_INLINE_COMMAND(MOUSE_COUNT)
GBX_INLINE_COMMAND(PIXEL_GET)
GBX_INLINE_COMMAND(SCREEN_HEIGHT)
GBX_INLINE_COMMAND(SCREEN_SIZE)
GBX_INLINE_COMMAND(SCREEN_WIDTH)
GBX_INLINE_COMMAND(SPRITE_SIZE)
GBX_INLINE_COMMAND(DELTA_TIME)
GBX_INLINE_COMMAND(ELAPSED_TIME)
GBX_INLINE_COMMAND(VIEWPORT)
GBX_INLINE_COMMAND(SPRITE_GRAB)
GBX_INLINE_COMMAND(WAIT_INPUT)
GBX_INLINE_COMMAND(SOUND_STOP_ALL)
GBX_INLINE_COMMAND(INPUT)
GBX_INLINE_COMMAND(INSTR)
GBX_INLINE_COMMAND(INTEGER)
GBX_INLINE_COMMAND(INTEGER64)
GBX_INLINE_COMMAND(SCREEN_FULLSCREEN)
GBX_INLINE_COMMAND(MUSIC_PLAYING)
GBX_INLINE_COMMAND(KEY_DOWN)
GBX_INLINE_COMMAND(KEY_PRESSED)
GBX_INLINE_COMMAND(KEY_WAIT)
GBX_INLINE_COMMAND(LEN)
GBX_INLINE_COMMAND(LIMIT_FPS)
GBX_INLINE_COMMAND(ANIM_LOAD)
GBX_INLINE_COMMAND(BACKGROUND_LOAD)
GBX_INLINE_COMMAND(FONT_LOAD)
GBX_INLINE_COMMAND(SOUND_LOAD)
GBX_INLINE_COMMAND(SPRITE_LOAD)
GBX_INLINE_COMMAND(LOGN)
GBX_INLINE_COMMAND(MAX)
GBX_INLINE_COMMAND(MIN)
GBX_INLINE_COMMAND(MOD)
GBX_INLINE_COMMAND(MOUSE_AXIS)
GBX_INLINE_COMMAND(MOUSE_PRESSED)
GBX_INLINE_COMMAND(MOUSE_STATE)
GBX_INLINE_COMMAND(MOUSE_WAIT)
GBX_INLINE_COMMAND(MUSIC_VOLUME)
GBX_INLINE_COMMAND(NUMBER)
GBX_INLINE_COMMAND(MUSIC_PAUSE)
GBX_INLINE_COMMAND(MUSIC_PLAY)
GBX_INLINE_COMMAND(SOUND_PLAY)
GBX_INLINE_COMMAND(POLY_STRIP)
GBX_INLINE_COMMAND(POLY_POINT)
GBX_INLINE_COMMAND(POW)
GBX_INLINE_COMMAND(SCREEN_SCALE)
GBX_INLINE_COMMAND(PRINT)
GBX_INLINE_COMMAND(SET_ENV)
GBX_INLINE_COMMAND(INSTR_REVERSE)
GBX_INLINE_COMMAND(RND)
GBX_INLINE_COMMAND(SPRITE_DRAW_ROTATED)
GBX_INLINE_COMMAND(ANIM_DRAW_ROTATED_SCALED)
GBX_INLINE_COMMAND(SPRITE_DRAW_ROTATED_SCALED)
GBX_INLINE_COMMAND(SCREEN_SAVE_BMP)
GBX_INLINE_COMMAND(SPRITE_SAVE)
GBX_INLINE_COMMAND(RND_SEED)
GBX_INLINE_COMMAND(FONT_SET)
GBX_INLINE_COMMAND(MOUSE_MOVE)
GBX_INLINE_COMMAND(PIXEL_SET)
GBX_INLINE_COMMAND(SCREEN_SET)
GBX_INLINE_COMMAND(ANIM_SET_TILES)
GBX_INLINE_COMMAND(TEXT_COLOR)
GBX_INLINE_COMMAND(SET_COLOR_KEY)
GBX_INLINE_COMMAND(WINDOW_TITLE)
GBX_INLINE_COMMAND(SGN)
GBX_INLINE_COMMAND(SCREEN_SHOW)
GBX_INLINE_COMMAND(SIN)
GBX_INLINE_COMMAND(SLEEP)
GBX_INLINE_COMMAND(SET_SMOOTHING)
GBX_INLINE_COMMAND(SOUND_PLAYING)
GBX_INLINE_COMMAND(SPLIT_STRING)
GBX_INLINE_COMMAND(SPRITE_COLLIDE)
GBX_INLINE_COMMAND(SQR)
GBX_INLINE_COMMAND(POLY_BEGIN)
GBX_INLINE_COMMAND(STDERR)
GBX_INLINE_COMMAND(STDOUT)
GBX_INLINE_COMMAND(MUSIC_STOP)
GBX_INLINE_COMMAND(SOUND_STOP)
GBX_INLINE_COMMAND(ANIM_DRAW_STRETCHED)
GBX_INLINE_COMMAND(SPRITE_DRAW_STRETCHED)
GBX_INLINE_COMMAND(SET_SYSTEM_POINTER)
GBX_INLINE_COMMAND(TAN)
GBX_INLINE_COMMAND(SCREEN_TO_BACKGROUND)
GBX_INLINE_COMMAND(SCREEN_USE)
GBX_INLINE_COMMAND(VAL)
GBX_INLINE_COMMAND(SET_VIEWPORT)
GBX_INLINE_COMMAND(SPRITE_DRAW_SCALED)

#undef GBX_INLINE_COMMAND

}  // namespace gbxinline

#define GBX_INLINE_CAT_(a, b) a##b
#define GBX_INLINE_CAT(a, b) GBX_INLINE_CAT_(a, b)

/* Register a function with GBX: name, min arguments, max arguments
 * (-1 = any number), function pointer. */
#define GBX_INLINE_FN(name, minArgs, maxArgs, fn) \
    static ::gbxinline::Registrar GBX_INLINE_CAT(gbx_inline_reg_, __LINE__)(name, minArgs, maxArgs, fn)

/* Short form without an argument check. */
#define GBX_INLINE_FN_ANY(name, fn) GBX_INLINE_FN(name, 0, -1, fn)

#endif /* __cplusplus */

#endif /* GBX_INLINE_H */
