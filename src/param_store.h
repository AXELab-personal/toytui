// param_store.h — the seed of the parameter-handling module.
//
// Stores the parameters of a single element in flat key-value form; the tree
// (parent-child relation, unset resolution by tree walk, the two-step
// subcomponent -> own element -> up the tree chain for box subcomponents) is the
// job of `ElementTree`. `ParamValue`/`ParamState` were designed so that
// the types here need no rewriting for that.
#pragma once

#include <map>
#include <string>

namespace toytui {

enum class ParamState { Unset, Set };

struct ParamValue {
    ParamState state = ParamState::Unset;
    std::string value; // meaningful only in Set state
};

// All parameters of an element (currently set or explicitly set to unset).
// For `core@terminal` every parameter handled here is always
// Set (core's size/colour is never unset).
class ElementParams {
public:
    void set(const std::string& name, std::string value) {
        params_[name] = ParamValue{ParamState::Set, std::move(value)};
    }

    // Removes the name completely (for transient markers that must not be stored).
    void remove(const std::string& name) { params_.erase(name); }

    void markUnset(const std::string& name) {
        params_[name] = ParamValue{ParamState::Unset, {}};
    }

    // nullptr if nobody has ever set the name (neither as Set nor as
    // explicit Unset) — this differs from the explicit Unset
    // state.
    const ParamValue* find(const std::string& name) const {
        auto it = params_.find(name);
        return it == params_.end() ? nullptr : &it->second;
    }

    // The STORED value of the parameter if Set; nullptr if missing OR explicitly Unset (the two are
    // equally "not set" here). The empty string is a Set value too. Where the distinction between Set/Unset/missing
    // matters (serializer, inheritance resolution), `find` remains.
    const std::string* getSet(const std::string& name) const {
        const ParamValue* v = find(name);
        return (v != nullptr && v->state == ParamState::Set) ? &v->value : nullptr;
    }

    // The Set value, otherwise `fallback`.
    std::string text(const std::string& name, const std::string& fallback = std::string()) const {
        const std::string* v = getSet(name);
        return v != nullptr ? *v : fallback;
    }

    // Whether a bool parameter "is switched on": true only in Set state with the "on"
    // (canonical) value — shared helper of box/label.
    bool isOn(const std::string& name) const {
        const ParamValue* v = find(name);
        return v != nullptr && v->state == ParamState::Set && v->value == "on";
    }

    // Every recorded parameter (Set and explicit Unset too) — for
    // serialization: the explicit unset must be stored too.
    const std::map<std::string, ParamValue>& all() const { return params_; }

private:
    std::map<std::string, ParamValue> params_;
};

} // namespace toytui
