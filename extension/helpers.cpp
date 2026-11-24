#include "helpers.h"
#include <itoolentity.h>
#include <cstring>
#include <limits>

extern IServerTools *servertools;

int nothing;

static cell_t* GetPawnAddressCells(IPluginContext* context, cell_t local)
{
	if (context->GetRuntime()->FindPubvarByName("__Int64_Address__", nullptr) != SP_ERROR_NONE)
	{
		context->ThrowNativeError("Recompile this plugin with the SourceMod 1.13 includes to use Address");
		return nullptr;
	}

	cell_t* cells = nullptr;
	if (int error = context->LocalToPhysAddr(local, &cells); error != SP_ERROR_NONE)
	{
		context->ThrowNativeErrorEx(error, "Invalid Address argument");
		return nullptr;
	}
	return cells;
}

void* ReadPawnAddress(IPluginContext* context, cell_t local)
{
	const cell_t* cells = GetPawnAddressCells(context, local);
	if (!cells)
		return nullptr;

	uint64_t address;
	std::memcpy(&address, cells, sizeof(address));
	if (address > std::numeric_limits<uintptr_t>::max())
	{
		context->ThrowNativeError("Address does not fit in a native pointer");
		return nullptr;
	}
	return reinterpret_cast<void*>(static_cast<uintptr_t>(address));
}

cell_t WritePawnAddress(IPluginContext* context, cell_t local, const void* ptr)
{
	if (cell_t* cells = GetPawnAddressCells(context, local))
	{
		const uint64_t address = reinterpret_cast<uintptr_t>(ptr);
		std::memcpy(cells, &address, sizeof(address));
	}
	return 0;
}

bool GetEngineAddress(IGameConfig* config, const char* name, void** address)
{
	if (!config->GetAddress(name, address) || !*address)
		return false;

	// Some x64 gamedata addresses point to a RIP-relative displacement in code.
	int offset;
	const std::string key = std::string(name) + "::rip";
	if (config->GetOffset(key.c_str(), &offset))
	{
		const auto* displacement = static_cast<const uint8_t*>(*address) + offset;
		int32_t relative;
		std::memcpy(&relative, displacement, sizeof(relative));
		*address = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(displacement) + sizeof(relative) + relative);
	}
	return true;
}

void MatrixToPawnMatrix(IPluginContext* context, cell_t* matAddr, const matrix3x4_t& mat)
{
	for ( int r = 0; r < 3; r++ )
	{
		cell_t* row = nullptr;
		if (context->GetRuntime()->UsesDirectArrays())
		{
			context->LocalToPhysAddr(matAddr[r], &row);
		}
		else
		{
			row = (cell_t *)( (uint8_t *)( &matAddr[r] ) + matAddr[r] );
		}

		for ( int c = 0; c < 4; c++ )
		{
			row[c] = sp_ftoc( mat[r][c] );
		}
	}
}

void PawnMatrixToMatrix(IPluginContext* context, cell_t* matAddr, matrix3x4_t& mat)
{
	for ( int r = 0; r < 3; r++ )
	{
		cell_t* row = nullptr;
		if (context->GetRuntime()->UsesDirectArrays())
		{
			context->LocalToPhysAddr(matAddr[r], &row);
		}
		else
		{
			row = (cell_t *)( (uint8_t *)( &matAddr[r] ) + matAddr[r] );
		}

		for ( int c = 0; c < 4; c++ )
		{
			mat[r][c] = sp_ctof( row[c] );
		}
	}
}

const char *HandleErrorToString(HandleError err)
{
	switch(err)
	{
		case HandleError_None: { return "No error"; }
		case HandleError_Changed: { return "The handle has been freed and reassigned"; }
		case HandleError_Type: { return "The handle has a different type registered"; }
		case HandleError_Freed: { return "The handle has been freed"; }
		case HandleError_Index: { return "generic internal indexing error"; }
		case HandleError_Access: { return "No access permitted to free this handle"; }
		case HandleError_Limit: { return "The limited number of handles has been reached"; }
		case HandleError_Identity: { return "The identity token was not usable"; }
		case HandleError_Owner: { return "Owners do not match for this operation"; }
		case HandleError_Version: { return "Unrecognized security structure version"; }
		case HandleError_Parameter: { return "An invalid parameter was passed"; }
		case HandleError_NoInherit: { return "This type cannot be inherited"; }
	}

	return "";
}

string_t AllocPooledString(const char *pszValue)
{
	CBaseEntity* pEntity = ((IServerUnknown*)servertools->FirstEntity())->GetBaseEntity();
	datamap_t* pDataMap = gamehelpers->GetDataMap( pEntity );

	static int offset = -1;
	if (offset == -1)
	{
		sm_datatable_info_t info;
		bool found = gamehelpers->FindDataMapInfo(pDataMap, "m_iName", &info);
		offset = info.actual_offset;
	}

	string_t *pProp = (string_t*)((intp)pEntity + offset);
	string_t backup = *pProp;
	servertools->SetKeyValue(pEntity, "targetname", pszValue);
	string_t newString = *pProp;
	*pProp = backup;
	return newString;
}

#ifndef __linux__
struct RTTIBaseClassArray;
struct TypeDescriptor;
// http://www.openrce.org/articles/full_view/23
struct RTTIClassHierarchyDescriptor
{
	DWORD signature;
	DWORD attributes;
	DWORD numBaseClasses;
	struct RTTIBaseClassArray* pBaseClassArray;
};
struct RTTICompleteObjectLocator
{
	DWORD signature;
	DWORD offset;
	DWORD cdOffset;
	struct TypeDescriptor* pTypeDescriptor;
	struct RTTIClassHierarchyDescriptor* pClassDescriptor;
};
#else
class __class_type_info;
// https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/libsupc%2B%2B/tinfo.h
struct vtable_prefix
{
  ptrdiff_t whole_object;
#ifdef _GLIBCXX_VTABLE_PADDING
  ptrdiff_t padding1;               
#endif
  const __class_type_info *whole_type;  
#ifdef _GLIBCXX_VTABLE_PADDING
  ptrdiff_t padding2;               
#endif
  const void *origin;               
};
#endif

void** vtable_dup(void* thisPtr, size_t numFuncs)
{

#ifndef __linux__
	size_t size = sizeof(RTTICompleteObjectLocator*) + (sizeof(void*) * numFuncs);
#else
	size_t size = sizeof(vtable_prefix) + (sizeof(void*) * numFuncs);
#endif

	uint8_t* newvtable = (uint8_t*)calloc(1, size);
	uint8_t* vtable = *(uint8_t**)thisPtr;

#ifndef __linux__
	uint8_t* vtable_start = vtable - sizeof(RTTICompleteObjectLocator*);
#else
	uint8_t* vtable_start = vtable - sizeof(vtable_prefix);
#endif
	memcpy(newvtable, vtable_start, size);

#ifndef __linux__
	newvtable += sizeof(RTTICompleteObjectLocator*);
#else
	newvtable += sizeof(vtable_prefix);
#endif

	return (void **)newvtable;
}

void vtable_replace(void* thisPtr, void** newVtable)
{
	*(void ***)thisPtr = newVtable;
}