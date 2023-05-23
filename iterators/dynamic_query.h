#pragma once

#include "../components/component.h"
#include "../utils/fetchers.h"
#include "core/string/string_name.h"
#include "core/templates/local_vector.h"

class World;
class ComponentDynamicExposer;

namespace godex {

enum SelectOperator : int {
	WITH = 0,
	WITHOUT,
	MAYBE,
	CHANGED,
};

enum DynamicQueryElementContainer : int {
	WITH_CONTAINER = 0,
	ANY_CONTAINER,
	JOIN_CONTAINER,
};


class DynamicQuerySelectElement {
public:
	godex::component_id id;
	/// Used to get by component name.
	StringName name;
	StorageBase *storage;
	bool mutability;
	bool fetch_enabled = true;
	EntityList changed;

	LocalVector<SelectOperator> opers;
	DynamicQueryElementContainer container = WITH_CONTAINER;

	bool is_filter_determinant() const;
	EntitiesBuffer get_entities();
	bool filter_satisfied(EntityID p_entity) const;

	void prepare_world(World *p_world);
	void initiate_process(World *p_world);
	void conclude_process(World *p_world);

	bool can_fetch() {return fetch_enabled;}
	void fetch(EntityID p_entity, Space p_space, ComponentDynamicExposer &p_accessor);

private:
	EntitiesBuffer _get_entities_with_oper(int64_t p_oper_idx);
	bool _filter_satisfied_with_oper(EntityID p_entity, int64_t p_oper_idx) const;
};

class DynamicQuerySelect {

protected:
	LocalVector<DynamicQuerySelectElement *> select_elements;

public:
	void add_element(DynamicQuerySelectElement *p_element) {
		select_elements.push_back(p_element);
	}

	void prepare_world(World *p_world) {
		for (uint32_t i = 0; i < select_elements.size(); i += 1) {
			select_elements[i]->prepare_world(p_world);
		}
	}
	void initiate_process(World *p_world) {
		for (uint32_t i = 0; i < select_elements.size(); i += 1) {
			select_elements[i]->initiate_process(p_world);
		}
	}
	void conclude_process(World *p_world) {
		for (uint32_t i = 0; i < select_elements.size(); i += 1) {
			select_elements[i]->conclude_process(p_world);
		}
	}

	bool all_determinant() const {
		for (uint32_t i = 0; i < select_elements.size(); i += 1) {
			if (!select_elements[i]->is_filter_determinant()) {
				return false;
			}
		}
		return true;
	}

	bool any_determinant() const {
		for (uint32_t i = 0; i < select_elements.size(); i += 1) {
			if (select_elements[i]->is_filter_determinant()) {
				return true;
			}
		}
		return false;
	}

	virtual EntitiesBuffer get_entities() {
		return EntitiesBuffer(UINT32_MAX, nullptr);
	}
	virtual bool filter_satisfied(EntityID p_entity) const {
		return true;
	}
};

class DynamicQuerySelectWith : public DynamicQuerySelect {
public:
	virtual EntitiesBuffer get_entities();
	virtual bool filter_satisfied(EntityID p_entity) const;
};

struct DynamicQuerySelectAny : public DynamicQuerySelect {
	EntityList entities;
public:

	virtual EntitiesBuffer get_entities();
	virtual bool filter_satisfied(EntityID p_entity) const;
};

/// This query is slower compared to `Query` but can be builded at runtime, so
/// that the scripts can still interact with the `World`.
/// Cache this query allow to save the time needed to lookup the components IDs,
/// so it's advised store it and use when needed.
class DynamicQuery : public GodexWorldFetcher {
	GDCLASS(DynamicQuery, GodexWorldFetcher)

	bool valid = true;
	bool can_change = true;
	Space space = Space::LOCAL;
	LocalVector<DynamicQuerySelectElement> elements;
	LocalVector<ComponentDynamicExposer> accessors;
	LocalVector<DynamicQuerySelect *> selects;

	World *world = nullptr;
	uint32_t iterator_index = 0;
	EntityID current_entity;
	EntitiesBuffer entities = EntitiesBuffer(0, nullptr);

	static void _bind_methods();

public:
	DynamicQuery();
	~DynamicQuery();

	/// Set the fetch mode of this query.
	void set_space(Space p_space);

	/// Add component.
	void with_component(uint32_t p_component_id, bool p_mutable = false);
	void maybe_component(uint32_t p_component_id, bool p_mutable = false);
	void changed_component(uint32_t p_component_id, bool p_mutable = false);

	/// Excludes this component from the query.
	void not_component(uint32_t p_component_id);

	void _with_component(uint32_t p_component_id, bool p_mutable);


	uint32_t select_component(uint32_t p_component_id, bool p_mutable = false);
	uint32_t without(uint32_t p_component_id);
	uint32_t maybe(uint32_t p_component_id);
	uint32_t changed(uint32_t p_component_id);
	void any(const PackedInt64Array &p_element_ids);

	uint32_t _insert_element_oper(uint32_t p_component_id, SelectOperator oper);

	// uint32_t any(uint32_t p_component_id);
	// uint32_t join(uint32_t p_component_id);

	/// Returns true if this query is valid.
	bool is_valid() const;

	/// Clear the query so this memory can be reused.
	void reset();

	uint32_t access_count() const;
	/// The returned pointer is valid only for the execution of the query.
	/// If you reset the query, copy it (move the object), this pointer is invalidated.
	Object *get_access_by_index_gd(uint32_t p_index) const;
	ComponentDynamicExposer *get_access_by_index(uint32_t p_index) const;

	virtual void get_system_info(SystemExeInfo *p_info) const override;

	void prepare_world_script(Object *p_world);
	void begin_script(Object *p_world);
	void end_script();

	virtual void prepare_world(World *p_world) override;
	virtual void initiate_process(World *p_world) override;
	virtual void conclude_process(World *p_world) override;
	virtual void release_world(World *p_world) override;
	virtual void set_active(bool p_active) override;

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Iterator

	/// Advance entity
	bool next();

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Random Access
	bool script_has(uint32_t p_id) const;
	bool has(EntityID p_id) const;

	void script_fetch(uint32_t p_entity);
	void fetch(EntityID p_entity);

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Utilities
	/// Returns entity id.
	uint32_t script_get_current_entity_id() const;
	EntityID get_current_entity_id() const;
	uint32_t count() const;

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Set / Get / Call
	virtual void setvar(const Variant &p_key, const Variant &p_value, bool *r_valid = nullptr) override;
	virtual Variant getvar(const Variant &p_key, bool *r_valid = nullptr) const override;

	int64_t find_element_by_name(const StringName &p_name) const;
	int64_t find_element_by_component_id(const uint32_t &p_component_id) const;
};
} // namespace godex