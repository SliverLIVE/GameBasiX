/* gbxarray.h - dynamic arrays for GameBasiX
 *
 * A list that grows on its own.
 * It lives in C++ (std::vector) and is included via INLINE:
 *
 *     INLINE
 *     #include "gbxarray.h"
 *     ENDINLINE
 *
 *     list = GBXARRAY_NEW()
 *     GBXARRAY_ADD list, 42
 *     PRINT GBXARRAY_GET(list, 0) + "", 10, 10
 *
 * An array can hold numbers (INTEGER, FLOAT) and text side by side; every
 * item remembers what it is. The commands (all names are BASIC names):
 *
 *   GBXARRAY_NEW()                  handle (1 …) for a new array
 *   GBXARRAY_FREE h                 1 = freed
 *   GBXARRAY_COUNT(h)               number of items
 *   GBXARRAY_ADD h, value           appends, returns the new index
 *   GBXARRAY_INSERT h, i, value     inserts before index i, 1 = ok
 *   GBXARRAY_SET h, i, value        replaces an item, 1 = ok
 *   GBXARRAY_GET(h, i)              value; numbers as numbers, text as text
 *   GBXARRAY_REMOVE h, i            removes, 1 = ok
 *   GBXARRAY_CLEAR h                empties the array, 1 = ok
 *   GBXARRAY_FIND(h, value)         first index of the value, otherwise -1
 *   GBXARRAY_SORT h                 sorts (numbers before text), 1 = ok
 *   GBXARRAY_JOIN$(h, separator)    all items as one text
 *
 * Indices start at 0; negative values count from the end (-1 = last
 * element), exactly like the built-in GBX arrays.
 */
#ifndef GBXARRAY_H
#define GBXARRAY_H

#include <gbx_inline.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace gbxarray {

/** One item: number or text - depending on what was put in. */
struct Item {
    bool isText = false;
    double number = 0.0;
    std::string text;
};

using List = std::vector<Item>;

/** All arrays of the program, keyed by handle. */
inline std::unordered_map<int, List>& store() {
    static std::unordered_map<int, List> arrays;
    return arrays;
}

inline int& nextHandle() {
    static int handle = 0;
    return handle;
}

/** Array for a handle, or nullptr. */
inline List* find(int handle) {
    auto it = store().find(handle);
    return it == store().end() ? nullptr : &it->second;
}

/** Format a number like PRINT: whole values without decimals. */
inline std::string numberText(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.15g", value);
    std::string out(buffer);
    return out == "-0" ? std::string("0") : out;
}

/** Convert a BASIC index into a real one (negative counts from the end). */
inline bool resolve(const List& list, double raw, size_t& out) {
    long index = static_cast<long>(raw);
    if (index < 0) index += static_cast<long>(list.size());
    if (index < 0 || index >= static_cast<long>(list.size())) return false;
    out = static_cast<size_t>(index);
    return true;
}

inline void pushResult(GbxValue* ret, const Item& item) {
    if (item.isText) {
        gbxinline::retText(ret, item.text);
    } else {
        gbxinline::retNum(ret, item.number);
    }
}

/* ------------------------------------------------------------ commands */

inline void fnNew(const GbxValue*, int, GbxValue* ret) {
    const int handle = ++nextHandle();
    store()[handle] = List{};
    gbxinline::retNum(ret, handle);
}

inline void fnFree(const GbxValue* args, int argc, GbxValue* ret) {
    const int handle = static_cast<int>(gbxinline::argNum(args, argc, 0));
    gbxinline::retNum(ret, store().erase(handle) > 0 ? 1 : 0);
}

inline void fnCount(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    gbxinline::retNum(ret, list ? static_cast<double>(list->size()) : 0.0);
}

inline void fnAdd(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    if (!list) {
        gbxinline::retNum(ret, -1);
        return;
    }
    Item item;
    if (gbxinline::argIsText(args, argc, 1)) {
        item.isText = true;
        item.text = gbxinline::argText(args, argc, 1);
    } else {
        item.number = gbxinline::argNum(args, argc, 1);
    }
    list->push_back(item);
    gbxinline::retNum(ret, static_cast<double>(list->size() - 1));
}

inline void fnInsert(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    if (!list) {
        gbxinline::retNum(ret, 0);
        return;
    }
    long index = static_cast<long>(gbxinline::argNum(args, argc, 1));
    if (index < 0) index += static_cast<long>(list->size()) + 1;
    if (index < 0 || index > static_cast<long>(list->size())) {
        gbxinline::retNum(ret, 0);
        return;
    }
    Item item;
    if (gbxinline::argIsText(args, argc, 2)) {
        item.isText = true;
        item.text = gbxinline::argText(args, argc, 2);
    } else {
        item.number = gbxinline::argNum(args, argc, 2);
    }
    list->insert(list->begin() + index, item);
    gbxinline::retNum(ret, 1);
}

inline void fnSet(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    size_t index = 0;
    if (!list || !resolve(*list, gbxinline::argNum(args, argc, 1), index)) {
        gbxinline::retNum(ret, 0);
        return;
    }
    Item& item = (*list)[index];
    if (gbxinline::argIsText(args, argc, 2)) {
        item.isText = true;
        item.number = 0.0;
        item.text = gbxinline::argText(args, argc, 2);
    } else {
        item.isText = false;
        item.number = gbxinline::argNum(args, argc, 2);
        item.text.clear();
    }
    gbxinline::retNum(ret, 1);
}

inline void fnGet(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    size_t index = 0;
    if (!list || !resolve(*list, gbxinline::argNum(args, argc, 1), index)) {
        gbxinline::retNum(ret, 0);
        return;
    }
    pushResult(ret, (*list)[index]);
}

inline void fnRemove(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    size_t index = 0;
    if (!list || !resolve(*list, gbxinline::argNum(args, argc, 1), index)) {
        gbxinline::retNum(ret, 0);
        return;
    }
    list->erase(list->begin() + static_cast<long>(index));
    gbxinline::retNum(ret, 1);
}

inline void fnClear(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    if (list) list->clear();
    gbxinline::retNum(ret, list ? 1 : 0);
}

inline void fnFind(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    if (!list) {
        gbxinline::retNum(ret, -1);
        return;
    }
    const bool wantText = gbxinline::argIsText(args, argc, 1);
    for (size_t i = 0; i < list->size(); ++i) {
        const Item& item = (*list)[i];
        if (item.isText != wantText) continue;
        if (wantText) {
            if (item.text == gbxinline::argText(args, argc, 1)) {
                gbxinline::retNum(ret, static_cast<double>(i));
                return;
            }
        } else if (item.number == gbxinline::argNum(args, argc, 1)) {
            gbxinline::retNum(ret, static_cast<double>(i));
            return;
        }
    }
    gbxinline::retNum(ret, -1);
}

inline void fnSort(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    if (!list) {
        gbxinline::retNum(ret, 0);
        return;
    }
    std::stable_sort(list->begin(), list->end(), [](const Item& a, const Item& b) {
        if (a.isText != b.isText) return !a.isText;  // numbers first
        return a.isText ? a.text < b.text : a.number < b.number;
    });
    gbxinline::retNum(ret, 1);
}

inline void fnJoin(const GbxValue* args, int argc, GbxValue* ret) {
    List* list = find(static_cast<int>(gbxinline::argNum(args, argc, 0)));
    if (!list) {
        gbxinline::retText(ret, std::string());
        return;
    }
    const std::string separator = gbxinline::argText(args, argc, 1);
    std::string joined;
    for (size_t i = 0; i < list->size(); ++i) {
        if (i) joined += separator;
        const Item& item = (*list)[i];
        joined += item.isText ? item.text : numberText(item.number);
    }
    gbxinline::retText(ret, joined);
}

}  // namespace gbxarray

/* Register with GBX: name, min arguments, max arguments, function. */
GBX_INLINE_FN("GBXARRAY_NEW", 0, 0, gbxarray::fnNew);
GBX_INLINE_FN("GBXARRAY_FREE", 1, 1, gbxarray::fnFree);
GBX_INLINE_FN("GBXARRAY_COUNT", 1, 1, gbxarray::fnCount);
GBX_INLINE_FN("GBXARRAY_ADD", 2, 2, gbxarray::fnAdd);
GBX_INLINE_FN("GBXARRAY_INSERT", 3, 3, gbxarray::fnInsert);
GBX_INLINE_FN("GBXARRAY_SET", 3, 3, gbxarray::fnSet);
GBX_INLINE_FN("GBXARRAY_GET", 2, 2, gbxarray::fnGet);
GBX_INLINE_FN("GBXARRAY_REMOVE", 2, 2, gbxarray::fnRemove);
GBX_INLINE_FN("GBXARRAY_CLEAR", 1, 1, gbxarray::fnClear);
GBX_INLINE_FN("GBXARRAY_FIND", 2, 2, gbxarray::fnFind);
GBX_INLINE_FN("GBXARRAY_SORT", 1, 1, gbxarray::fnSort);
GBX_INLINE_FN("GBXARRAY_JOIN", 1, 2, gbxarray::fnJoin);

#endif /* GBXARRAY_H */
