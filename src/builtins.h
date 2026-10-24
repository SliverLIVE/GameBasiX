// GameBasiX - eingebaute Befehle und Funktionen
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime.h"

struct BArgs {
    Runtime& rt;
    const std::string& name;
    std::vector<Value>& vals;
    std::vector<Value*>& refs;
    SrcPos pos;

    // Argumentzugriff mit Bereichspruefung
    double num(size_t i) const;
    int64_t integer(size_t i) const;
    std::string str(size_t i) const;
    bool truthy(size_t i) const;
    Value* out(size_t i) const;         // Zielvariable fuer BYREF-Ausgaben
    void setOut(size_t i, Value v) const;
    void require(size_t n) const;
    [[noreturn]] void fail(const std::string& msg) const;
};

using BuiltinFn = std::function<Value(BArgs&)>;

struct Builtin {
    int minArgs = 0;
    int maxArgs = 0;
    BuiltinFn fn;
};

std::unordered_map<std::string, Builtin>& builtins();
