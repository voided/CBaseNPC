#include "pawn_tokens.h"

#ifdef KE_ARCH_X64
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "sourcesdk/basecombatcharacter.h"
#include "sourcesdk/nav_mesh.h"
#include "NextBotInterface.h"
#include "NextBotVisionInterface.h"
#include "NextBotKnownEntity.h"

namespace
{
struct PawnPointer
{
	const void* ptr;
	PawnTokenLifetime lifetime;
	CBaseHandle owner;
	std::function<bool()> isAlive;
};

std::unordered_map<const void*, cell_t> pawnTokens;
std::unordered_map<cell_t, PawnPointer> pawnPointers;
// CBaseHandle::ToInt() includes both the entity slot and its serial number.
std::unordered_multimap<uint32_t, cell_t> entityTokens;
std::vector<cell_t> frameTokens;
uint32_t nextPawnToken = 1;

void ErasePawnToken(std::unordered_map<cell_t, PawnPointer>::iterator entry)
{
	if (entry->second.owner.IsValid())
	{
		auto [first, last] = entityTokens.equal_range(entry->second.owner.ToInt());
		for (; first != last; ++first)
		{
			if (first->second == entry->first)
			{
				entityTokens.erase(first);
				break;
			}
		}
	}
	pawnTokens.erase(entry->second.ptr);
	pawnPointers.erase(entry);
}

cell_t RegisterPawnToken(const void* ptr, PawnTokenLifetime lifetime,
	CBaseHandle owner = {}, std::function<bool()> isAlive = {})
{
	if (!ptr)
		return 0;

	auto existing = pawnTokens.find(ptr);
	if (existing != pawnTokens.end())
	{
		const cell_t token = existing->second;
		if (PawnTokenToPtr(token))
		{
			auto entry = pawnPointers.find(token);
			auto& value = entry->second;
			if (owner.IsValid() && value.owner.IsValid() && owner != value.owner)
			{
				ErasePawnToken(entry);
			}
			else
			{
				const bool promote = value.lifetime == PawnTokenLifetime::Frame && lifetime != value.lifetime;
				if (promote || (value.lifetime == lifetime && !value.isAlive && isAlive))
				{
					if (owner.IsValid() && !value.owner.IsValid())
					{
						entityTokens.emplace(owner.ToInt(), token);
						value.owner = owner;
					}
					value.lifetime = lifetime;
					value.isAlive = std::move(isAlive);
				}
				return token;
			}
		}
	}

	// Retiring an ID is permanent, even across map changes and address reuse.
	if (nextPawnToken > static_cast<uint32_t>(std::numeric_limits<cell_t>::max()))
		return 0;

	const auto token = static_cast<cell_t>(nextPawnToken++);
	auto entry = pawnPointers.emplace(token, PawnPointer{ptr, lifetime, owner, std::move(isAlive)}).first;
	try
	{
		auto reverse = pawnTokens.emplace(ptr, token);
		if (!reverse.second)
		{
			pawnPointers.erase(entry);
			return reverse.first->second;
		}
		if (owner.IsValid())
			entityTokens.emplace(owner.ToInt(), token);
		if (lifetime == PawnTokenLifetime::Frame)
			frameTokens.push_back(token);
	}
	catch (...)
	{
		ErasePawnToken(entry);
		throw;
	}
	return token;
}
}

cell_t PtrToPawnToken(const void* ptr, PawnTokenLifetime lifetime)
{
	return RegisterPawnToken(ptr, lifetime);
}

cell_t PtrToPawnToken(const CNavArea* area)
{
	if (!area || !pTheNavAreas || pTheNavAreas->Find(const_cast<CNavArea*>(area)) == pTheNavAreas->InvalidIndex())
		return 0;

	const auto id = area->GetID();
	return RegisterPawnToken(area, PawnTokenLifetime::NavMesh, {}, [area, id] {
		return pTheNavAreas && pTheNavAreas->Find(const_cast<CNavArea*>(area)) != pTheNavAreas->InvalidIndex()
			&& area->GetID() == id;
	});
}

cell_t PtrToPawnToken(const CNavLadder* ladder)
{
	return PtrToPawnToken(ladder, PawnTokenLifetime::NavMesh);
}

cell_t PtrToPawnToken(const HidingSpot* spot)
{
	return PtrToPawnToken(spot, PawnTokenLifetime::NavMesh);
}

cell_t BorrowPawnToken(const void* ptr, std::function<bool()> isAlive)
{
	return RegisterPawnToken(ptr, PawnTokenLifetime::Frame, {}, std::move(isAlive));
}

cell_t TrackBotMemberPawnToken(const void* ptr, cell_t botToken, std::function<bool()> isAlive)
{
	auto bot = pawnPointers.find(botToken);
	if (bot == pawnPointers.end())
		return 0;

	return RegisterPawnToken(ptr, PawnTokenLifetime::Owned, bot->second.owner, std::move(isAlive));
}

void* PawnTokenToPtr(cell_t token)
{
	auto entry = pawnPointers.find(token);
	if (entry == pawnPointers.end())
		return nullptr;

	if (auto isAlive = entry->second.isAlive)
	{
		const bool live = isAlive();
		// Virtual engine calls made by a validator may reenter the extension.
		entry = pawnPointers.find(token);
		if (entry == pawnPointers.end())
			return nullptr;
		if (!live)
		{
			ErasePawnToken(entry);
			return nullptr;
		}
	}

	return const_cast<void*>(entry->second.ptr);
}

void ForgetPawnToken(const void* ptr)
{
	auto token = pawnTokens.find(ptr);
	if (token != pawnTokens.end())
		ErasePawnToken(pawnPointers.find(token->second));
}

void ForgetEntityPawnTokens(const CBaseEntity* entity)
{
	// SDKHooks calls the listener while the entity still exposes its handle.
	const auto key = static_cast<uint32_t>(entity->GetRefEHandle().ToInt());
	for (auto owner = entityTokens.find(key); owner != entityTokens.end(); owner = entityTokens.find(key))
		ErasePawnToken(pawnPointers.find(owner->second));
}

void AdvancePawnTokenFrame(bool)
{
	for (cell_t token : frameTokens)
	{
		auto entry = pawnPointers.find(token);
		if (entry != pawnPointers.end() && entry->second.lifetime == PawnTokenLifetime::Frame)
			ErasePawnToken(entry);
	}
	frameTokens.clear();
}

void ClearNavPawnTokens()
{
	for (auto it = pawnPointers.begin(); it != pawnPointers.end(); )
	{
		auto entry = it++;
		if (entry->second.lifetime == PawnTokenLifetime::NavMesh)
			ErasePawnToken(entry);
	}
}

void ClearMapPawnTokens()
{
	AdvancePawnTokenFrame(false);
	ClearNavPawnTokens();
	while (!entityTokens.empty())
		ErasePawnToken(pawnPointers.find(entityTokens.begin()->second));
}
#endif

cell_t BotToPawnToken(const INextBot* bot, CBaseEntity* entity, bool attached)
{
#ifdef KE_ARCH_X64
	if (!bot || !entity)
		return 0;

	const EHANDLE owner(entity);
	if (!owner.IsValid())
		return 0;

	return RegisterPawnToken(bot, PawnTokenLifetime::Owned, owner, [owner, attached, bot] {
		auto* current = owner.Get();
		return current && (!attached || current->MyNextBotPointer() == bot);
	});
#else
	return PtrToPawnToken(bot);
#endif
}

cell_t KnownEntityToPawnToken(IVision* vision, const CKnownEntity* known)
{
#ifdef KE_ARCH_X64
	if (!vision || !known || !known->GetEntity())
		return 0;
	auto* bot = vision->GetBot();
	auto* owner = bot ? bot->GetEntity() : nullptr;
	if (!owner)
		return 0;
	const cell_t parent = BotToPawnToken(bot, owner);
	const cell_t subject = gamehelpers->EntityToReference(known->GetEntity());
	return BorrowPawnToken(known, [parent, vision, subject, known] {
		auto* live = static_cast<INextBot*>(PawnTokenToPtr(parent));
		auto* target = gamehelpers->ReferenceToEntity(subject);
		return live && target && live->GetVisionInterface() == vision && vision->GetKnown(target) == known;
	});
#else
	return PtrToPawnToken(known);
#endif
}
