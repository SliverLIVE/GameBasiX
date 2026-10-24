// GameBasiX - Wertemodell
#include "value.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

double Value::asDouble() const {
    switch (type) {
        case VT::Int: return static_cast<double>(i);
        case VT::Float: return f;
        case VT::Str: return std::strtod(s.c_str(), nullptr);
        case VT::Null: return 0.0;
        default: return 0.0;
    }
}

int64_t Value::asInt() const {
    switch (type) {
        case VT::Int: return i;
        case VT::Float: return static_cast<int64_t>(f);
        case VT::Str: return static_cast<int64_t>(std::strtod(s.c_str(), nullptr));
        case VT::Null: return 0;
        default: return 0;
    }
}

std::string formatInt(int64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    return buf;
}

// Kuerzeste Darstellung, die den Wert exakt wiedergibt. 3.0 -> "3", 0.1 -> "0.1"
std::string formatNumber(double v) {
    if (std::isnan(v)) return "NAN";
    if (std::isinf(v)) return v > 0 ? "INF" : "-INF";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.15g", v);
    std::string out(buf);
    if (out == "-0") out = "0";
    // 1e+20 -> 1e+20 ist in Ordnung, aber "1e+06" ist unnoetig: nur grosse
    // Exponenten behalten.
    return out;
}

std::string Value::text() const {
    switch (type) {
        case VT::Null: return "";
        case VT::Int: return formatInt(i);
        case VT::Float: return formatNumber(f);
        case VT::Str: return s;
        case VT::Array: return "[Array]";
        case VT::Record: return rec && rec->def ? "[" + rec->def->name + "]" : "[TYPE]";
    }
    return "";
}

bool Value::truthy() const {
    switch (type) {
        case VT::Null: return false;
        case VT::Int: return i != 0;
        case VT::Float: return f != 0.0;
        case VT::Str: return !s.empty();
        case VT::Array: return arr && !arr->items.empty();
        case VT::Record: return rec != nullptr;
    }
    return false;
}

int TypeDef::fieldIndex(const std::string& fname) const {
    for (size_t k = 0; k < fields.size(); ++k)
        if (fields[k].name == fname) return static_cast<int>(k);
    return -1;
}

bool TypeDef::hasMethod(const std::string& mname) const {
    for (const auto& name : methods)
        if (name == mname) return true;
    return false;
}

Value defaultValueFor(VT t) {
    switch (t) {
        case VT::Int: return Value::mkInt(0);
        case VT::Float: return Value::mkFloat(0.0);
        case VT::Str: return Value::mkStr("");
        default: return Value::mkNull();
    }
}

Value coerceTo(const Value& v, VT target) {
    if (v.type == target) return v;
    switch (target) {
        case VT::Int: return Value::mkInt(v.asInt());
        case VT::Float: return Value::mkFloat(v.asDouble());
        case VT::Str: return Value::mkStr(v.text());
        default: return v;
    }
}

bool valueEquals(const Value& a, const Value& b) {
    if (a.isNum() && b.isNum()) return a.asDouble() == b.asDouble();
    if (a.type == VT::Str && b.type == VT::Str) return a.s == b.s;
    if (a.type == b.type) {
        switch (a.type) {
            case VT::Null: return true;
            case VT::Array: return a.arr == b.arr;
            case VT::Record: return a.rec == b.rec;
            default: break;
        }
    }
    // String <-> Zahl: Text vergleichen (implizite Konvertierung)
    if (a.isStr() || b.isStr()) return a.text() == b.text();
    return a.asDouble() == b.asDouble();
}
