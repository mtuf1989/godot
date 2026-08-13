/**************************************************************************/
/*  symphony_graph_package_retirement.cpp                                 */
/**************************************************************************/

#include "symphony_graph_package_retirement.h"

#include "servers/audio/audio_server.h"

std::atomic<PreparedGraphPackage *> GraphPackageRetirement::head{ nullptr };
std::atomic<uint32_t> GraphPackageRetirement::pending_count{ 0 };
bool GraphPackageRetirement::update_callback_registered = false;

void GraphPackageRetirement::_update_callback(void *p_userdata) {
	(void)p_userdata;
	drain();
}

void GraphPackageRetirement::initialize() {
	if (update_callback_registered) {
		return;
	}
	AudioServer *as = AudioServer::get_singleton();
	if (!as) {
		return;
	}
	as->add_update_callback(_update_callback, nullptr);
	update_callback_registered = true;
}

void GraphPackageRetirement::uninitialize() {
	if (update_callback_registered) {
		AudioServer *as = AudioServer::get_singleton();
		if (as) {
			as->remove_update_callback(_update_callback, nullptr);
		}
		update_callback_registered = false;
	}
	drain();
}

void GraphPackageRetirement::retire(PreparedGraphPackage *p_package) {
	if (!p_package) {
		return;
	}
	PreparedGraphPackage *old_head = head.load(std::memory_order_relaxed);
	do {
		p_package->retire_next = old_head;
	} while (!head.compare_exchange_weak(old_head, p_package, std::memory_order_release, std::memory_order_relaxed));
	const uint32_t pending = pending_count.fetch_add(1, std::memory_order_relaxed) + 1;
	symphony_note_retirement_pending(pending);
}

void GraphPackageRetirement::drain() {
	PreparedGraphPackage *list = head.exchange(nullptr, std::memory_order_acquire);
	uint32_t destroyed = 0;
	while (list) {
		PreparedGraphPackage *next = list->retire_next;
		list->retire_next = nullptr;
		PreparedGraphPackage::destroy(list);
		list = next;
		destroyed++;
	}
	if (destroyed > 0) {
		pending_count.fetch_sub(destroyed, std::memory_order_relaxed);
		symphony_note_packages_destroyed(destroyed);
	}
}

uint32_t GraphPackageRetirement::get_pending_count() {
	return pending_count.load(std::memory_order_relaxed);
}

uint64_t GraphPackageRetirement::get_destroyed_count() {
	return symphony_packages_destroyed_count().load(std::memory_order_relaxed);
}

uint32_t GraphPackageRetirement::get_peak_pending_count() {
	return symphony_retirement_peak_pending().load(std::memory_order_relaxed);
}
