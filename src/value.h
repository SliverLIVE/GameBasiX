// GameBasiX - Wertemodell
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class VT { Null, Int, Float, Str, Array, Record };

struct TypeDef;
struct ArrayObj;
struct RecordObj;

struct Value {
    VT type = VT::Null;
    int64_t i = 0;
    double f = 0.0;
    std::string s;
    std::shared_ptr<ArrayObj> arr;
    std::shared_ptr<RecordObj> rec;

    static Value mkNull() { return Value(); }
    static Value mkInt(int64_t v) {
        Value r; r.type = VT::Int; r.i = v; return r;
    }
    static Value mkFloat(double v) {
        Value r; r.type = VT::Float; r.f = v; return r;
    }
    static Value mkStr(std::string v) {
        Value r; r.type = VT::Str; r.s = std::move(v); return r;
    }
    static Value mkBool(bool v) { return mkInt(v ? 1 : 0); }
    static Value mkArray(std::shared_ptr<ArrayObj> a) {
        Value r; r.type = VT::Array; r.arr = std::move(a); return r;
    }
    static Value mkRecord(std::shared_ptr<RecordObj> r0) {
        Value r; r.type = VT::Record; r.rec = std::move(r0); return r;
    }

    bool isNull() const { return type == VT::Null; }
    bool isNum() const { return type == VT::Int || type == VT::Float; }
    bool isStr() const { return type == VT::Str; }
    bool isArray() const { return type == VT::Array; }
    bool isRecord() const { return type == VT::Record; }

    double asDouble() const;
    int64_t asInt() const;
    // Textdarstellung fuer PRINT / String-Verkettung
    std::string text() const;
    bool truthy() const;
};

struct ArrayObj {
    std::vector<Value> items;
    VT elemVT = VT::Float;   // Elementtyp fuer Zahlen/Strings
    bool isTypeArray = false;
    std::string elemType;    // Name des TYPE-Elements, falls isTypeArray
};

struct FieldDef {
    std::string name;
    VT type = VT::Int;
    std::string typeName;  // gesetzt, wenn das Feld ein TYPE ist
};

struct TypeDef {
    std::string name;
    std::vector<FieldDef> fields;
    std::vector<std::string> methods;  // Namen der METHODen zu diesem TYPE
    int fieldIndex(const std::string& fname) const;
    bool hasMethod(const std::string& mname) const;
};

struct RecordObj {
    const TypeDef* def = nullptr;
    std::vector<Value> fields;
};

// Hilfsfunktionen
std::string formatNumber(double v);
std::string formatInt(int64_t v);
Value defaultValueFor(VT t);
Value coerceTo(const Value& v, VT target);
bool valueEquals(const Value& a, const Value& b);
