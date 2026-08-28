#include "probe_cache.h"
#include "core/math/math_funcs.h"

ProbeCache::ProbeCache() {
}

ProbeCache::CellKey ProbeCache::_position_to_cell(const Vector3 &p_pos) const {
	float inv_size = 1.0f / config.cell_size;
	CellKey key;
	key.x = (int32_t)Math::floor(p_pos.x * inv_size);
	key.y = (int32_t)Math::floor(p_pos.y * inv_size);
	key.z = (int32_t)Math::floor(p_pos.z * inv_size);
	return key;
}

bool ProbeCache::lookup(const Vector3 &p_position, RoomProbeResult &r_result) const {
	CellKey key = _position_to_cell(p_position);
	const RoomProbeResult *found = cache.getptr(key);
	if (!found) {
		const_cast<ProbeCache *>(this)->last_metrics.misses++;
		return false;
	}

	// Check staleness.
	float age = current_time - found->timestamp;
	if (age > config.max_age_seconds) {
		const_cast<ProbeCache *>(this)->last_metrics.misses++;
		return false;
	}

	r_result = *found;
	const_cast<ProbeCache *>(this)->last_metrics.hits++;
	return true;
}

bool ProbeCache::would_hit(const Vector3 &p_position) const {
	// Same logic as lookup() but no metric side effects.
	CellKey key = _position_to_cell(p_position);
	const RoomProbeResult *found = cache.getptr(key);
	if (!found) {
		return false;
	}
	return (current_time - found->timestamp) <= config.max_age_seconds;
}

void ProbeCache::store(const Vector3 &p_position, const RoomProbeResult &p_result) {
	CellKey key = _position_to_cell(p_position);

	RoomProbeResult entry = p_result;
	entry.timestamp = current_time;

	// Evict if at capacity.
	if (!cache.has(key) && cache.size() >= config.max_entries) {
		_evict_oldest();
	}

	cache[key] = entry;
	last_metrics.total_entries = cache.size();
}

void ProbeCache::invalidate_all() {
	last_metrics.evictions += (int)cache.size(); // count what we are about to drop
	cache.clear();
	last_metrics.total_entries = 0;
}

void ProbeCache::invalidate_near(const Vector3 &p_position, float p_radius) {
	// Compute the cell range that the radius covers.
	float inv_size = 1.0f / config.cell_size;
	int range = (int)Math::ceil(p_radius * inv_size);
	CellKey center = _position_to_cell(p_position);

	Vector<CellKey> to_remove;
	for (int dx = -range; dx <= range; dx++) {
		for (int dy = -range; dy <= range; dy++) {
			for (int dz = -range; dz <= range; dz++) {
				CellKey k;
				k.x = center.x + dx;
				k.y = center.y + dy;
				k.z = center.z + dz;
				if (cache.has(k)) {
					to_remove.push_back(k);
				}
			}
		}
	}

	for (int i = 0; i < to_remove.size(); i++) {
		cache.erase(to_remove[i]);
		last_metrics.evictions++;
	}
	last_metrics.total_entries = cache.size();
}

void ProbeCache::_evict_oldest() {
	// Find the oldest entry and remove it.
	float oldest_time = current_time;
	CellKey oldest_key = {};
	bool found_any = false;

	for (const KeyValue<CellKey, RoomProbeResult> &kv : cache) {
		if (kv.value.timestamp < oldest_time) {
			oldest_time = kv.value.timestamp;
			oldest_key = kv.key;
			found_any = true;
		}
	}

	if (found_any) {
		cache.erase(oldest_key);
		last_metrics.evictions++;
	}
}
