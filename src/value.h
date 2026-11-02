// GameBasiX - Wertemodell
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class VT { Null, Int, Float, Str, Array, Record, Dict, Func };

struct TypeDef;
struct ArrayObj;
struct RecordObj;
struct DictObj;

struct Value {
    VT type = VT::Null;
    int64_t i = 0;
    double f = 0.0;
    std::string s;
    std::shared_ptr<ArrayObj> arr;
    std::shared_ptr<RecordObj> rec;
    std::shared_ptr<DictObj> dict;

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
    static Value mkDict(std::shared_ptr<DictObj> d) {
        Value r; r.type = VT::Dict; r.dict = std::move(d); return r;
    }
    // Funktionswert: s traegt den Namen der FUNCTION.
    static Value mkFunc(std::string name) {
        Value r; r.type = VT::Func; r.s = std::move(name); return r;
    }

    bool isNull() const { return type == VT::Null; }
    bool isNum() const { return type == VT::Int || type == VT::Float; }
    bool isStr() const { return type == VT::Str; }
    bool isArray() const { return type == VT::Array; }
    bool isRecord() const { return type == VT::Record; }
    bool isDict() const { return type == VT::Dict; }
    bool isFunc() const { return type == VT::Func; }

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

// Dictionary: Schluessel sind immer Text, die Werte beliebig. `keys` haelt die
// Reihenfolge der ersten Einfuegung, damit DICT_KEY$/FOREACH stabil laufen.
struct DictObj {
    std::vector<std::string> keys;
    std::unordered_map<std::string, Value> map;

    size_t size() const { return map.size(); }
    bool has(const std::string& k) const { return map.find(k) != map.end(); }
    void set(const std::string& k, Value v) {
        auto it = map.find(k);
        if (it == map.end()) keys.push_back(k);
        map[k] = std::move(v);
    }
    void erase(const std::string& k) {
        if (map.erase(k) == 0) return;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == k) {
                keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    void clear() {
        map.clear();
        keys.clear();
    }
};

struct FieldDef {
    std::string name;
    VT type = VT::Int;
    std::string typeName;  // gesetzt, wenn das Feld ein TYPE ist
    // Feld-Array ("items[4] AS ITEM"): type/typeName beschreiben dann das
    // Element, rank die Anzahl der Klammern. Die Groessen stehen als
    // Ausdruecke in Program::typeArrayDims, weil value.h keinen Syntaxbaum
    // kennen soll.
    bool isArray = false;
    int rank = 0;
};

struct TypeDef {
    std::string name;
    std::vector<FieldDef> fields;
    std::vector<std::string> methods;  // Namen der METHODen zu diesem TYPE
    // Vererbung: "TYPE B EXTENDS A". Nach dem Aufloesen (resolveTypes) sind
    // die Felder der Basis bereits in `fields` enthalten; `base` zeigt nur
    // noch fuer die Methodensuche und ISA() auf die Basis.
    std::string baseName;
    TypeDef* base = nullptr;
    std::vector<std::string> interfaces;  // "TYPE B IMPLEMENTS I, J"

    int fieldIndex(const std::string& fname) const;
    bool hasMethod(const std::string& mname) const;     // mit Basis
    bool hasOwnMethod(const std::string& mname) const;  // nur dieser TYPE
    // "ist ein A" - der TYPE selbst oder eine seiner Basen.
    bool isA(const std::string& tname) const;
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
