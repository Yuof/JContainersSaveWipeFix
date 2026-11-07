// Fixes the race in JContainers that costs a save all its JContainers data, or crashes the save (on 4.3.3:
// JContainers64.dll+1911D8 `mov rax,[r9]`, r9 = 0, inside boost's basic_oarchive save_pointer).
//
// The autorelease queue (its own thread, a 2 s tick) checks `refCount() <= 1` with no lock, zeroes _aqueue_refCount and
// calls object_base::_delete_self, which takes the registry's write lock, removes the object and deletes it. A Papyrus
// call that fetches the object in between (getObjectRef: the read lock, +1 stack ref) links it into a map, so the map
// holds a deleted object. The next save walks that map: typeid on the freed block throws, JContainers catches it and the
// save gets no JContainers data at all (the load reports `0 objects total`) - or, when the block still has object_base's
// vtable, boost dereferences a null serializer and the game crashes.
//
// _delete_self now re-checks the owners once it holds the write lock: getObjectRef can't hand the object out any more,
// so the answer stays true until the object is out of the registry. Owners found: the lock is released and the object
// stays, exactly as if the queue's check had seen them (it is queued again when its last owner lets go). Nobody owning
// it: everything runs as before. The same change in JContainers' source: https://github.com/ryobg/JContainers/pull/142
//
// One `call` is retargeted, inside _delete_self (every caller goes through it: the queue releases, which zero
// _aqueue_refCount first, and the GC on load, which only passes objects with no owners). The code is unchanged since
// 4.1.12; each Nexus build is matched by its DLL name, PE timestamp and a hash of the whole function. Any other build:
// inactive, nothing patched.

#include <expected>
#include <format>

#include <xbyak/xbyak.h>

namespace
{
	struct Build
	{
		std::wstring_view module;
		std::string_view  name;        // JContainers version (game runtime), from the Nexus file list of mod 16495
		std::uint32_t     timestamp;   // PE header
		std::uintptr_t    deleteSelf;  // RVAs: object_base::_delete_self, hashed over `length` bytes (FNV-1a 64)
		std::size_t       length;
		std::uint64_t     hash;
		std::uintptr_t    lockCall;  // `call` of the write_lock constructor (rcx = guard, rdx = mutex, rdi = the object)
		std::uintptr_t    unlock;    // the guard's unlock, then `delete this` as a tail call through [rdi]'s vtable
	};

	// Two shapes of the function: 4.2.0 and later (0x1D0 bytes), 4.1.x (0xEC). Both keep the object in rdi, read
	// nothing else on the way from the unlock to the delete, and carry no relocations (the hash is position-independent)
	constexpr std::array<Build, 20> kBuilds{ {
		{ L"JContainers64.dll"sv, "4.3.4 (SE 1.5.97)"sv, 0x6ABFD382, 0xC1030, 0x1d0, 0xEB32F48028D6942D, 0xC1052, 0xC1194 },
		{ L"JContainers64.dll"sv, "4.3.3 (AE 1.6.1170)"sv, 0x6ABE899F, 0xC1070, 0x1d0, 0x810384320414C17D, 0xC1092, 0xC11D4 },
		{ L"JContainers64.dll"sv, "4.3.2 (AE 1.7.104)"sv, 0x6A926CD8, 0xC1070, 0x1d0, 0x810384320414C17D, 0xC1092, 0xC11D4 },
		{ L"JContainers64.dll"sv, "4.3.0 (AE 1.6.1170)"sv, 0x6A8D5968, 0xC1050, 0x1d0, 0x810384320414C17D, 0xC1072, 0xC11B4 },
		{ L"JContainers64.dll"sv, "4.2.13.1 (AE 1.6.1170)"sv, 0x691AC971, 0xBF5A0, 0x1d0, 0x26CEF2F722B37C67, 0xBF5C2, 0xBF704 },
		{ L"JContainers64.dll"sv, "4.2.9 (AE 1.6.1170)"sv, 0x65ABFEB4, 0xBAB70, 0x1d0, 0xCE83B04DE2445272, 0xBAB92, 0xBACD4 },
		{ L"JContainers64.dll"sv, "4.2.8 (AE 1.6.1130)"sv, 0x65751A86, 0xBAB70, 0x1d0, 0xCE83B04DE2445272, 0xBAB92, 0xBACD4 },
		{ L"JContainers64.dll"sv, "4.2.7 (AE 1.6.1130)"sv, 0x65702691, 0xBAB70, 0x1d0, 0xCE83B04DE2445272, 0xBAB92, 0xBACD4 },
		{ L"JContainers64.dll"sv, "4.2.3 (AE 1.6.640)"sv, 0x6333F021, 0xBAB78, 0x1d0, 0xB3B10E3AA5FD18E2, 0xBAB9A, 0xBACDC },
		{ L"JContainers64.dll"sv, "4.2.2 (AE 1.6.353)"sv, 0x61D951CD, 0xBAB88, 0x1d0, 0x47DD30F418B217C2, 0xBABAA, 0xBACEC },
		{ L"JContainers64.dll"sv, "4.2.1 (AE 1.6.342)"sv, 0x61B8B69A, 0xBAB88, 0x1d0, 0x47DD30F418B217C2, 0xBABAA, 0xBACEC },
		{ L"JContainers64.dll"sv, "4.2.0 (AE 1.6.323)"sv, 0x61A4DCCF, 0xBAB88, 0x1d0, 0x47DD30F418B217C2, 0xBABAA, 0xBACEC },
		{ L"JContainers64.dll"sv, "4.1.13 (SE 1.5.97)"sv, 0x5F44DDA8, 0xC5584, 0xec, 0xF2FCCF516ED8F292, 0xC55AC, 0xC5607 },
		{ L"JContainers64.dll"sv, "4.1.12 (SE 1.5.97)"sv, 0x5DD680A7, 0xC5594, 0xec, 0xF2FCCF516ED8F292, 0xC55BC, 0xC5617 },
		{ L"JContainersGOG.dll"sv, "4.3.2 (GOG 1.6.1179)"sv, 0x6A926F48, 0xC1050, 0x1d0, 0x810384320414C17D, 0xC1072, 0xC11B4 },
		{ L"JContainersGOG.dll"sv, "4.2.12 (GOG 1.6.1179)"sv, 0x65DE3023, 0xBAB70, 0x1d0, 0xCE83B04DE2445272, 0xBAB92, 0xBACD4 },
		{ L"JContainersGOG.dll"sv, "4.2.6 (GOG 1.6.659)"sv, 0x63AAAD48, 0xBAB78, 0x1d0, 0xB3B10E3AA5FD18E2, 0xBAB9A, 0xBACDC },
		{ L"JContainersVR.dll"sv, "4.3.2 (VR 1.4.15)"sv, 0x6A9269BA, 0xC1000, 0x1d0, 0x2C18DBB4AAB640B4, 0xC1022, 0xC1164 },
		{ L"JContainersVR.dll"sv, "4.2.11 (VR 1.4.15)"sv, 0x65DDBC75, 0xBAB20, 0x1d0, 0x29AA112850CC3F45, 0xBAB42, 0xBAC84 },
		{ L"JContainersVR.dll"sv, "4.1.13 (VR 1.4.15)"sv, 0x5F44E7A8, 0xC5554, 0xec, 0xF2FCCF516ED8F292, 0xC557C, 0xC55D7 },
	} };
	constexpr std::array<std::wstring_view, 3> kModules{ L"JContainers64.dll"sv, L"JContainersVR.dll"sv, L"JContainersGOG.dll"sv };

	struct Object  // the start of collections::object_base, the same in every build above
	{
		void*                      vtbl;
		std::atomic<std::uint32_t> id;
		std::atomic<std::int32_t>  ref, tes, stack, aqueue;
	};
	static_assert(offsetof(Object, ref) == 0xC && offsetof(Object, aqueue) == 0x18);

	using Lock_t = void* (*)(void*, void*);
	Lock_t lockFn = nullptr;

	std::atomic<std::uint64_t> kept{ 0 };

	bool Milestone(std::uint64_t a_n)  // 1, 10, 100, ...
	{
		while (a_n % 10 == 0) {
			a_n /= 10;
		}
		return a_n == 1;
	}

	// An owner moving between two counters (stack -> map) changes both: one pass over the four can miss it, so the sum
	// only counts when two passes agree
	std::int32_t Owners(const Object* a_obj)
	{
		for (;;) {
			const std::int32_t r = a_obj->ref, t = a_obj->tes, s = a_obj->stack, a = a_obj->aqueue;
			if (r == a_obj->ref && t == a_obj->tes && s == a_obj->stack && a == a_obj->aqueue) {
				return r + t + s + a;
			}
		}
	}

	// Test builds only: JC_WIDEN_MS sleeps inside the race window until the first race, JC_WIDEN_ONLY leaves the fix out
	void Widen([[maybe_unused]] const Object* a_obj)
	{
#ifdef JC_WIDEN_MS
		static std::atomic<bool> raced{ false };
		if (!raced) {
			Sleep(JC_WIDEN_MS);
			if (const auto n = Owners(a_obj); n > 0) {
				raced = true;
				logger::info("TEST race: object id {} gained {} owner(s) in the window"sv, a_obj->id.load(), n);
			}
		}
#endif
	}

	// In place of the write_lock constructor. True: the object has owners, keep it (the write lock is still held)
	bool LockAndCheck(void* a_guard, void* a_mutex, const Object* a_obj)
	{
		Widen(a_obj);
		lockFn(a_guard, a_mutex);
#ifdef JC_WIDEN_ONLY
		return false;
#else
		if (Owners(a_obj) <= 0) {
			return false;
		}
		if (const auto n = ++kept; Milestone(n)) {
			logger::info("object id {} got an owner while the queue was deleting it - kept ({} so far)"sv, a_obj->id.load(), n);
		}
		return true;
#endif
	}

	// What the keep path hands to _delete_self's tail call in place of the object: `delete this` lands here
	void NoDelete(void*, std::uint32_t) {}
	using NoDelete_t = decltype(&NoDelete);
	const NoDelete_t        noDeleteVtbl[1]{ NoDelete };
	const NoDelete_t* const noDeleteObject = noDeleteVtbl;

	// Called in place of the lock constructor, rdi = the object. Keep: return into _delete_self's own unlock instead,
	// with rdi = the stand-in, so the original unlock runs and only the delete is gone (_delete_self pops rdi afterwards)
	struct Stub : Xbyak::CodeGenerator
	{
		Stub(void* a_where, std::size_t a_size, std::uintptr_t a_unlock) :
			Xbyak::CodeGenerator(a_size, a_where)
		{
			Xbyak::Label done;
			sub(rsp, 0x28);
			mov(r8, rdi);
			mov(rax, reinterpret_cast<std::uintptr_t>(&LockAndCheck));
			call(rax);
			add(rsp, 0x28);
			test(al, al);
			jz(done);
			mov(rdi, reinterpret_cast<std::uintptr_t>(&noDeleteObject));
			mov(rax, a_unlock);
			mov(ptr[rsp], rax);
			L(done);
			ret();
		}
	};

	std::uint64_t Fnv1a64(const std::uint8_t* a_data, std::size_t a_size)
	{
		std::uint64_t h = 0xCBF29CE484222325;
		for (std::size_t i = 0; i < a_size; ++i) {
			h = (h ^ a_data[i]) * 0x100000001B3;
		}
		return h;
	}

	// Executable memory a rel32 from a_next can reach, or null. Not CommonLib's Trampoline::create: it ends the process
	// when it finds none, and it searches relative to its argument without knowing where the call is
	void* AllocInReach(std::uintptr_t a_next, std::size_t a_size)
	{
		constexpr std::uintptr_t kGranularity = 0x10000;
		const std::uintptr_t     kReach = 0x7FFF0000;  // under 2 GB, so any block starting inside it is in reach
		const auto               last = a_next + kReach;
		MEMORY_BASIC_INFORMATION mbi{};
		for (auto addr = a_next - kReach; addr < last; addr = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
			if (!VirtualQuery(reinterpret_cast<const void*>(addr), &mbi, sizeof(mbi))) {
				return nullptr;
			}
			const auto start = (addr + kGranularity - 1) & ~(kGranularity - 1);
			const auto end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
			if (mbi.State == MEM_FREE && start + a_size <= end && start + a_size <= last) {
				if (auto* mem = VirtualAlloc(reinterpret_cast<void*>(start), a_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) {
					return mem;
				}
			}
		}
		return nullptr;
	}

	// the patched build's name, or why nothing was patched
	std::expected<std::string_view, std::string> Hook()
	{
		std::uintptr_t jc = 0;
		std::wstring_view module;
		for (const auto m : kModules) {
			if ((jc = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(m.data())))) {
				module = m;
				break;
			}
		}
		if (!jc) {
			return std::unexpected("JContainers is not loaded"s);
		}
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(jc + reinterpret_cast<const IMAGE_DOS_HEADER*>(jc)->e_lfanew);
		const auto  stamp = nt->FileHeader.TimeDateStamp;
		const auto  build = std::ranges::find_if(kBuilds, [&](const Build& b) { return b.module == module && b.timestamp == stamp; });
		if (build == kBuilds.end()) {
			return std::unexpected(std::format("not a JContainers build this plugin knows (timestamp {:08X})", stamp));
		}
		if (Fnv1a64(reinterpret_cast<const std::uint8_t*>(jc + build->deleteSelf), build->length) != build->hash) {
			return std::unexpected(std::format("JContainers {}: the code differs (changed by another plugin?)", build->name));
		}
		const auto next = build->lockCall + jc + 5;
		lockFn = reinterpret_cast<Lock_t>(next + *reinterpret_cast<const std::int32_t*>(jc + build->lockCall + 1));

		constexpr std::size_t kStubSize = 0x80;
		auto*                 where = AllocInReach(next, kStubSize);
		if (!where) {
			return std::unexpected(std::format("JContainers {}: no free memory in reach", build->name));
		}
		Stub stub{ where, kStubSize, jc + build->unlock };

		const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(where) - static_cast<std::intptr_t>(next));
		REL::safe_write(jc + build->lockCall + 1, &rel, sizeof(rel));
		return build->name;
	}
}

SKSEPluginInfo(
	.Version = Plugin::VERSION,
	.Name = Plugin::NAME,
	.Author = "Yuof"sv,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = true });
	logger::info("{} v{} on {}"sv, Plugin::NAME, Plugin::VERSION.string(), REL::Module::get().version().string());
	// JContainers may load after this plugin: patch once every plugin is loaded (its queue isn't ticking yet)
	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		if (a_msg->type != SKSE::MessagingInterface::kPostLoad) {
			return;
		}
		if (const auto build = Hook()) {
			logger::info("self-check passed, JContainers {}: _delete_self re-checks the owners under the registry lock"sv, *build);
		} else {
			logger::error("self-check failed ({}): inactive, nothing patched"sv, build.error());
		}
	});
	return true;
}
