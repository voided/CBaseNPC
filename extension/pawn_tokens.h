#pragma once

#include "smsdk_ext.h"
#include <functional>

// SourcePawn methodmaps have one cell of storage even when Address is an int64.
enum class PawnTokenLifetime { Frame, NavMesh, Owned };

class INextBot;
class CKnownEntity;
class IVision;
class CNavArea;
class CNavLadder;
class HidingSpot;

#ifdef KE_ARCH_X64
cell_t PtrToPawnToken(const void* ptr, PawnTokenLifetime lifetime = PawnTokenLifetime::Frame);
cell_t PtrToPawnToken(const CNavArea* area);
cell_t PtrToPawnToken(const CNavLadder* ladder);
cell_t PtrToPawnToken(const HidingSpot* spot);
cell_t BorrowPawnToken(const void* ptr, std::function<bool()> isAlive);
cell_t TrackBotMemberPawnToken(const void* ptr, cell_t botToken, std::function<bool()> isAlive);
void* PawnTokenToPtr(cell_t token);
void ForgetPawnToken(const void* ptr);
void ForgetEntityPawnTokens(const CBaseEntity* entity);
void ClearNavPawnTokens();
void ClearMapPawnTokens();
void AdvancePawnTokenFrame(bool simulating);
#else
inline cell_t PtrToPawnToken(const void* ptr, PawnTokenLifetime = PawnTokenLifetime::Frame) { return reinterpret_cast<uintptr_t>(ptr); }
inline cell_t BorrowPawnToken(const void* ptr, std::function<bool()>) { return PtrToPawnToken(ptr); }
inline void* PawnTokenToPtr(cell_t token) { return reinterpret_cast<void*>(static_cast<uintptr_t>(token)); }
inline void ForgetPawnToken(const void*) {}
inline void ForgetEntityPawnTokens(const CBaseEntity*) {}
inline void ClearNavPawnTokens() {}
inline void ClearMapPawnTokens() {}
inline void AdvancePawnTokenFrame(bool) {}
#endif

cell_t BotToPawnToken(const INextBot* bot, CBaseEntity* entity, bool attached = true);
cell_t KnownEntityToPawnToken(IVision* vision, const CKnownEntity* known);

template<typename Member>
cell_t BotMemberToPawnToken(INextBot* bot, CBaseEntity* owner, Member member, Member (INextBot::*getter)() const)
{
#ifdef KE_ARCH_X64
	const auto parent = BotToPawnToken(bot, owner);
	return TrackBotMemberPawnToken(member, parent, [parent, getter, member] {
		auto* live = static_cast<INextBot*>(PawnTokenToPtr(parent));
		return live && (live->*getter)() == member;
	});
#else
	return PtrToPawnToken(member);
#endif
}
