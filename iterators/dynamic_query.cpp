#include "dynamic_query.h"

#include "../ecs.h"
#include "../modules/godot/nodes/ecs_world.h"

using godex::DynamicQueryElementContainer;
using godex::SelectOperator;
using godex::DynamicQuerySelectElement;
using godex::DynamicQuerySelectWith;
using godex::DynamicQuerySelectAny;
using godex::DynamicQuery;

bool DynamicQuerySelectElement::is_filter_determinant() const {
	switch (opers[opers.size() - 1]) {
		case WITH: {
			return true;
		} break;
		case CHANGED: {
			return true;
		} break;
	}

	return false;
}

bool DynamicQuerySelectElement::filter_satisfied(EntityID p_entity) const {
	return _filter_satisfied_with_oper(p_entity, opers.size() - 1);
}

bool DynamicQuerySelectElement::_filter_satisfied_with_oper(EntityID p_entity, int64_t p_oper_idx) const {
	if (p_oper_idx < 0) {
		return true;
	}

	switch (opers[p_oper_idx]) {
		case WITH: {
			if (unlikely(storage == nullptr)) {
				return false;
			}
			return storage->has(p_entity);
		} break;
		case WITHOUT: {
			if (unlikely(storage == nullptr)) {
				return false;
			}
			return _filter_satisfied_with_oper(p_entity, p_oper_idx - 1) == false;
		} break;
		case CHANGED: {
			if (unlikely(storage == nullptr)) {
				return false;
			}
			return changed.has(p_entity);
		} break;
		case MAYBE: {
			return true;
		}break;
	}

	return true;
}

EntitiesBuffer DynamicQuerySelectElement::get_entities() {
	return _get_entities_with_oper(opers.size() - 1);
}

EntitiesBuffer DynamicQuerySelectElement::_get_entities_with_oper(int64_t p_oper_idx) {
	EntitiesBuffer e(UINT32_MAX, nullptr);
	if ( unlikely(storage == nullptr) || p_oper_idx < 0 ) {
		return e;
	}

	switch (opers[p_oper_idx]) {
		case WITH: {
			e = storage->get_stored_entities();
		} break;
		case WITHOUT: {
		} break;
		case CHANGED: {
			e = EntitiesBuffer(changed.size(), changed.get_entities_ptr());
		} break;
		case MAYBE: {
		}break;
	}

	return e;
}

void DynamicQuerySelectElement::prepare_world(World *p_world) {
	if (opers.find(CHANGED) != -1) {
		p_world->get_storage(id)->add_change_listener(&changed);
	}
}
void DynamicQuerySelectElement::initiate_process(World *p_world) {
	changed.freeze();
	storage = p_world->get_storage(id);
}
void DynamicQuerySelectElement::conclude_process(World *p_world) {
	storage = nullptr;
	changed.unfreeze();
	changed.clear();
}

void DynamicQuerySelectElement::fetch(EntityID p_entity, Space p_space, ComponentDynamicExposer &p_accessor) {
	if ( can_fetch() && likely(storage != nullptr) && storage->has(p_entity)) {
		if (p_accessor.is_mutable()) {
			p_accessor.set_target(storage->get_ptr(p_entity, p_space));
		} else {
			// Taken using the **CONST** `get_ptr` function, but casted back
			// to mutable. The `Accessor` already guards its accessibility
			// so it's safe do so.
			// Note: this is used by GDScript, we don't need that this is
			// const at compile time.
			// Note: since we have to storage mutable, it's safe cast this
			// data back to mutable.
			// Note: `std::as_const` doesn't work here. The compile is
			// optimizing it? Well, I'm just using `const_cast`.
			const void *c(const_cast<const StorageBase *>(storage)->get_ptr(p_entity, p_space));
			p_accessor.set_target(const_cast<void *>(c));
		}
	} else {
		// This data not found, just set nullptr.
		p_accessor.set_target(nullptr);
	}
}


EntitiesBuffer DynamicQuerySelectWith::get_entities() {
	EntitiesBuffer e(UINT32_MAX, nullptr);
	for (uint32_t i=0; i < select_elements.size(); i += 1) {
		if (select_elements[i]->is_filter_determinant()) {
			EntitiesBuffer eb = select_elements[i]->get_entities();
			if (eb.count < e.count) {
				e = eb;
			}
		}
	}

	return e;
}

bool DynamicQuerySelectWith::filter_satisfied(EntityID p_entity) const {
	for (uint32_t i=0; i < select_elements.size(); i += 1) {
		if (!select_elements[i]->filter_satisfied(p_entity)) {
			return false;
		}
	}
	return true;
}

EntitiesBuffer DynamicQuerySelectAny::get_entities() {
	entities.clear();

	if (any_determinant()) {
		for (uint32_t i = 0; i < select_elements.size(); i += 1) {
			EntitiesBuffer eb = select_elements[i]->get_entities();
			if (eb.count != UINT32_MAX) {
				for (uint32_t j = 0; j < eb.count; j += 1) {
					entities.insert(eb.entities[j]);
				}
			}
		}
		return EntitiesBuffer (entities.size(), entities.get_entities_ptr());
	}

	return EntitiesBuffer(UINT32_MAX, nullptr);
}

bool DynamicQuerySelectAny::filter_satisfied(EntityID p_entity) const {
	if (all_determinant()) {
		return entities.has(p_entity);
	} else {
		for (uint32_t i=0; i < select_elements.size(); i += 1) {
			if (select_elements[i]->filter_satisfied(p_entity)) {
				return true;
			}
		}
		return false;
	}
}

void DynamicQuery::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_space", "space"), &DynamicQuery::set_space);
	ClassDB::bind_method(D_METHOD("with_component", "component_id", "is_mutable"), &DynamicQuery::with_component);
	ClassDB::bind_method(D_METHOD("maybe_component", "component_id", "is_mutable"), &DynamicQuery::maybe_component);
	ClassDB::bind_method(D_METHOD("changed_component", "component_id", "is_mutable"), &DynamicQuery::changed_component);
	ClassDB::bind_method(D_METHOD("not_component", "component_id"), &DynamicQuery::not_component);

	ClassDB::bind_method(D_METHOD("select_component", "component_id", "is_mutable"), &DynamicQuery::select_component);
	ClassDB::bind_method(D_METHOD("without", "component_id"), &DynamicQuery::without);
	ClassDB::bind_method(D_METHOD("maybe", "component_id"), &DynamicQuery::maybe);
	ClassDB::bind_method(D_METHOD("changed", "component_id"), &DynamicQuery::changed);
	ClassDB::bind_method(D_METHOD("any", "component_ids"), &DynamicQuery::any);


	ClassDB::bind_method(D_METHOD("is_valid"), &DynamicQuery::is_valid);
	ClassDB::bind_method(D_METHOD("prepare_world", "world"), &DynamicQuery::prepare_world_script);
	ClassDB::bind_method(D_METHOD("reset"), &DynamicQuery::reset);
	ClassDB::bind_method(D_METHOD("get_component", "index"), &DynamicQuery::get_access_by_index_gd);

	ClassDB::bind_method(D_METHOD("begin", "world"), &DynamicQuery::begin_script);
	ClassDB::bind_method(D_METHOD("end"), &DynamicQuery::end_script);

	ClassDB::bind_method(D_METHOD("next"), &DynamicQuery::next);

	ClassDB::bind_method(D_METHOD("has", "entity_index"), &DynamicQuery::script_has);
	ClassDB::bind_method(D_METHOD("fetch", "entity_index"), &DynamicQuery::script_fetch);

	ClassDB::bind_method(D_METHOD("get_current_entity_id"), &DynamicQuery::script_get_current_entity_id);
	ClassDB::bind_method(D_METHOD("count"), &DynamicQuery::count);
}

DynamicQuery::DynamicQuery() {
}

void DynamicQuery::set_space(Space p_space) {
	space = p_space;
}

uint32_t DynamicQuery::select_component(uint32_t p_component_id, bool p_mutable) {
	_with_component(p_component_id, p_mutable);
	return p_component_id;
}

uint32_t DynamicQuery::without(uint32_t p_component_id) {
	return _insert_element_oper(p_component_id, WITHOUT);
}

uint32_t DynamicQuery::maybe(uint32_t p_component_id) {
	return _insert_element_oper(p_component_id, MAYBE);
}

uint32_t DynamicQuery::changed(uint32_t p_component_id) {
	return _insert_element_oper(p_component_id, CHANGED);
}

void DynamicQuery::any(const PackedInt32Array &p_component_ids) {

	for (int32_t i = 0; i < p_component_ids.size(); i += 1) {
		int64_t element_idx = find_element_by_component_id(p_component_ids[i]);
		ERR_FAIL_COND_MSG(element_idx == -1, "The component id " + itos(p_component_ids[i]) + " need to be selected first.");
		ERR_FAIL_COND_MSG(elements[element_idx].container != WITH_CONTAINER, "The component id " + itos(p_component_ids[i]) + " already in 'any' or 'join'.");
		elements[element_idx].container = ANY_CONTAINER;
	}

	DynamicQuerySelectAny *select_any = memnew(DynamicQuerySelectAny);

	for (int32_t i = 0; i < p_component_ids.size(); i += 1) {
		int64_t element_idx = find_element_by_component_id(p_component_ids[i]);
		select_any->add_element(&elements[element_idx]);
	}

	selects.push_back(select_any);
}

uint32_t DynamicQuery::_insert_element_oper(uint32_t p_component_id, SelectOperator oper) {
	int64_t element_idx = find_element_by_component_id(p_component_id);
	ERR_FAIL_COND_V_MSG(element_idx == -1, p_component_id, "The component id " + itos(p_component_id) + " need to be selected first.");
	elements[element_idx].opers.push_back(oper);
	return p_component_id;
}

void DynamicQuery::with_component(uint32_t p_component_id, bool p_mutable) {
	select_component(p_component_id, p_mutable);
}

void DynamicQuery::maybe_component(uint32_t p_component_id, bool p_mutable) {
	maybe(select_component(p_component_id, p_mutable));
}

void DynamicQuery::changed_component(uint32_t p_component_id, bool p_mutable) {
	changed(select_component(p_component_id, p_mutable));
}

void DynamicQuery::not_component(uint32_t p_component_id) {
	without(select_component(p_component_id, false));
}

void DynamicQuery::_with_component(uint32_t p_component_id, bool p_mutable) {
	ERR_FAIL_COND_MSG(is_valid() == false, "This query is not valid.");
	ERR_FAIL_COND_MSG(can_change == false, "This query can't change at this point, you have to `clear` it.");
	if (unlikely(ECS::verify_component_id(p_component_id) == false)) {
		// Invalidate.
		valid = false;
		ERR_FAIL_MSG("The component_id " + itos(p_component_id) + " is invalid.");
	}

	ERR_FAIL_COND_MSG(find_element_by_name(ECS::get_component_name(p_component_id)) != -1, "The component " + itos(p_component_id) + " is already part of this query.");

	DynamicQuerySelectElement data;
	data.id = p_component_id;
	data.name = ECS::get_component_name(p_component_id);
	data.mutability = p_mutable;
	data.opers.push_back(WITH);
	elements.push_back(data);
}

bool DynamicQuery::is_valid() const {
	return valid;
}

void DynamicQuery::reset() {
	valid = true;
	can_change = true;
	elements.reset();

	for (uint32_t i = 0; i < selects.size(); i += 1) {
		memdelete(selects[i]);
	}
	selects.reset();
	world = nullptr;
}

DynamicQuery::~DynamicQuery(){
	for (uint32_t i = 0; i < selects.size(); i += 1) {
		memdelete(selects[i]);
	}
}

uint32_t DynamicQuery::access_count() const {
	return elements.size();
}

Object *DynamicQuery::get_access_by_index_gd(uint32_t p_index) const {
	ERR_FAIL_COND_V_MSG(is_valid() == false, nullptr, "The query is invalid.");
	ERR_FAIL_UNSIGNED_INDEX_V_MSG(p_index, accessors.size(), nullptr, "The index is not found.");
	return (Object *)(accessors.ptr() + p_index);
}

ComponentDynamicExposer *DynamicQuery::get_access_by_index(uint32_t p_index) const {
	ERR_FAIL_COND_V_MSG(is_valid() == false, nullptr, "The query is invalid.");
	return (ComponentDynamicExposer *)(accessors.ptr() + p_index);
}

void DynamicQuery::get_system_info(SystemExeInfo *p_info) const {
	ERR_FAIL_COND(is_valid() == false);
	for (uint32_t i = 0; i < elements.size(); i += 1) {
		if (elements[i].mutability) {
			p_info->mutable_components.insert(elements[i].id);
		} else {
			p_info->immutable_components.insert(elements[i].id);
		}
	}
}

void DynamicQuery::prepare_world_script(Object *p_world) {
	WorldECS *world = Object::cast_to<WorldECS>(p_world);
	ERR_FAIL_COND_MSG(world == nullptr, "The given object is not a `WorldECS`.");
	prepare_world(world->get_world());
}

void DynamicQuery::begin_script(Object *p_world) {
	WorldECS *world = Object::cast_to<WorldECS>(p_world);
	ERR_FAIL_COND_MSG(world == nullptr, "The given object is not a `WorldECS`.");
	initiate_process(world->get_world());
}

void DynamicQuery::end_script() {
	conclude_process(nullptr);
}

void DynamicQuery::prepare_world(World *p_world) {
	if (likely(can_change == false)) {
		// Already done.
		return;
	}
	ERR_FAIL_COND(is_valid() == false);

	can_change = false;
	world = p_world;

	// Build the access_component in this way the `ObjectDB` doesn't
	// complain, otherwise it needs to use pointers	(AccessComponent is a parent
	// of Object).
	accessors.resize(elements.size());
	for (uint32_t i = 0; i < elements.size(); i += 1) {
		accessors[i].init(
				elements[i].id,
				elements[i].mutability);
		elements[i].prepare_world(p_world);
	}


	// Put all 'with' elements togather.
	DynamicQuerySelectWith *select_with = memnew(DynamicQuerySelectWith);
	for (uint32_t i = 0; i < elements.size(); i += 1) {
		if (elements[i].container == WITH_CONTAINER) {
			select_with->add_element(&elements[i]);
		}
	}

	selects.push_back(select_with);
}

void DynamicQuery::initiate_process(World *p_world) {
	// Make sure the Query is build at this point.
	prepare_world(p_world);

	current_entity = EntityID();
	iterator_index = 0;
	entities.count = 0;

	ERR_FAIL_COND(is_valid() == false);

	// storages.resize(elements.size());
	entities.count = UINT32_MAX;

	for (uint32_t i = 0; i < selects.size(); i += 1) {
		selects[i]->initiate_process(p_world);
		EntitiesBuffer eb = selects[i]->get_entities();
		if (eb.count < entities.count) {
			entities = eb;
		}
	}

	if (unlikely(entities.count == UINT32_MAX)) {
		entities.count = 0;
		valid = false;
		ERR_PRINT("The Query can't be used if there are only non determinant filters (like `Without` and `Maybe`).");
	}

	// The Query is ready to fetch, let's rock!
}

void DynamicQuery::conclude_process(World *p_world) {
	for (uint32_t i = 0; i < selects.size(); i += 1) {
		selects[i]->conclude_process(p_world);
	}

	// Clear any component reference.
	// storages.clear();
	iterator_index = 0;
	entities.count = 0;
}

void DynamicQuery::release_world(World *p_world) {
	world = nullptr;
}

void DynamicQuery::set_active(bool p_active) {}

bool DynamicQuery::next() {
	// Search the next Entity to fetch.
	while (iterator_index < entities.count) {
		const EntityID entity_id = entities.entities[iterator_index];
		iterator_index += 1;

		if (has(entity_id)) {
			fetch(entity_id);
			return true;
		}
	}

	// Nothing more to fetch.
	return false;
}

bool DynamicQuery::script_has(uint32_t p_id) const {
	return has(p_id);
}

bool DynamicQuery::has(EntityID p_id) const {

	for (uint32_t i = 0; i < selects.size(); i += 1) {
		if (selects[i]->filter_satisfied(p_id) == false) {
			return false;
		}
	}

	// This entity can be fetched.
	return true;
}

void DynamicQuery::script_fetch(uint32_t p_entity_id) {
#ifdef DEBUG_ENABLED
	ERR_FAIL_COND_MSG(has(p_entity_id) == false, "[FATAL] This entity " + itos(p_entity_id) + " can't be fetched by this query. Please check it using the functin `has`.");
#endif
	fetch(p_entity_id);
}

void DynamicQuery::fetch(EntityID p_entity_id) {
	for (uint32_t i = 0; i < elements.size(); i += 1) {
		elements[i].fetch(p_entity_id, space, accessors[i]);
	}
	current_entity = p_entity_id;
}

uint32_t DynamicQuery::script_get_current_entity_id() const {
	return get_current_entity_id();
}

EntityID DynamicQuery::get_current_entity_id() const {
	return current_entity;
}

uint32_t DynamicQuery::count() const {
	uint32_t count = 0;
	for (uint32_t i = 0; i < entities.count; i += 1) {
		if (has(entities.entities[i])) {
			count += 1;
		}
	}
	return count;
}

void DynamicQuery::setvar(const Variant &p_key, const Variant &p_value, bool *r_valid) {
	*r_valid = true;
	// Assume valid, nothing to do.
}

Variant DynamicQuery::getvar(const Variant &p_key, bool *r_valid) const {
	if (p_key.get_type() == Variant::INT) {
		Object *obj = get_access_by_index_gd(p_key);
		if (obj == nullptr) {
			*r_valid = false;
			return Variant();
		} else {
			*r_valid = true;
			return obj;
		}
	} else if (p_key.get_type() == Variant::STRING_NAME) {
		const int64_t index = find_element_by_name(p_key);
		if (index >= 0) {
			*r_valid = true;
			return get_access_by_index_gd(index);
		} else {
			*r_valid = false;
			return Variant();
		}
	} else if (p_key.get_type() == Variant::STRING) {
		const int64_t index = find_element_by_name(StringName(p_key.operator String()));
		if (index >= 0) {
			*r_valid = true;
			return get_access_by_index_gd(index);
		} else {
			*r_valid = false;
			return Variant();
		}
	} else {
		*r_valid = false;
		ERR_PRINT("The proper syntax is: `query[0].my_component_variable`.");
		return Variant();
	}
}

int64_t DynamicQuery::find_element_by_name(const StringName &p_name) const {
	for (uint32_t i = 0; i < elements.size(); i += 1) {
		if (elements[i].name == p_name) {
			return i;
		}
	}
	return -1;
}

int64_t DynamicQuery::find_element_by_component_id(const uint32_t &p_component_id) const {
	for (uint32_t i = 0; i < elements.size(); i += 1) {
		if (elements[i].id == p_component_id) {
			return i;
		}
	}
	return -1;
}
