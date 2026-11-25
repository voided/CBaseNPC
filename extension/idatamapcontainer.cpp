
#include "idatamapcontainer.h"
#include "helpers.h"
#include <cstring>
#include <memory>
#include <new>
#include <mathlib/vmatrix.h>
#include "baseentityoutput.h"

IDataMapContainer::IDataMapContainer()
{
	m_pDataMap = nullptr;
	m_bHasDataMapDesc = false;
	m_DataMapDescSizeInBytes = 0;
}

IDataMapContainer::~IDataMapContainer()
{
}

datamap_t* IDataMapContainer::CreateDataDescMap(datamap_t* pBaseMap)
{
	if (!m_pDataMap)
	{
		const char* classname = GetDataDescMapClass();

		m_pDataMap = new datamap_t;
		m_pDataMap->baseMap = pBaseMap;
		m_pDataMap->dataClassName = classname ? strdup(classname) : nullptr;
		m_pDataMap->packed_offsets_computed = false;
		m_pDataMap->packed_size = 0;

		int numDataDesc = m_vecEntityDataTypeDescriptors.Count();
		typedescription_t * dataDesc = new typedescription_t[numDataDesc];
		m_pDataMap->dataDesc = dataDesc;
		m_pDataMap->dataNumFields = numDataDesc;

		for (int i = 0; i < numDataDesc; i++)
		{
			dataDesc[i] = m_vecEntityDataTypeDescriptors[i];
		}
	}

	return m_pDataMap;
}

void IDataMapContainer::DestroyDataDescMap()
{
	if (!m_pDataMap)
		return;
	
	m_DataMapCache.Purge();

	if (m_pDataMap->dataClassName)
		delete m_pDataMap->dataClassName;
	
	delete[] m_pDataMap->dataDesc;
	delete m_pDataMap;

	m_pDataMap = nullptr;
}

void IDataMapContainer::DestroyDataTypeDescriptor(typedescription_t *desc) const
{
	if (desc->fieldName)
	{
		free((void*)desc->fieldName);
		desc->fieldName = nullptr;
	}

	if (desc->externalName)
	{
		free((void*)desc->externalName);
		desc->externalName = nullptr;
	}
}

void IDataMapContainer::DestroyDataDesc()
{
	DestroyDataDescMap();

	m_bHasDataMapDesc = false;

	for (int i = 0; i < m_vecEntityDataTypeDescriptors.Count(); i++) 
	{
		DestroyDataTypeDescriptor(&m_vecEntityDataTypeDescriptors[i]);
	}

	m_vecEntityDataTypeDescriptors.Purge();
}

void IDataMapContainer::BeginDataDesc()
{
	DestroyDataDesc();
}

namespace
{
struct DataMapFieldLayout
{
	size_t size;
	size_t alignment;
};

template <typename T>
constexpr DataMapFieldLayout FieldLayout()
{
	return {sizeof(T), alignof(T)};
}

constexpr DataMapFieldLayout g_DataMapFieldLayouts[] = {
	{},                         // FIELD_VOID
	FieldLayout<float>(),        // FIELD_FLOAT
	FieldLayout<string_t>(),     // FIELD_STRING
	FieldLayout<Vector>(),       // FIELD_VECTOR
	FieldLayout<Quaternion>(),   // FIELD_QUATERNION
	FieldLayout<int>(),          // FIELD_INTEGER
	FieldLayout<bool>(),         // FIELD_BOOLEAN
	FieldLayout<short>(),        // FIELD_SHORT
	FieldLayout<char>(),         // FIELD_CHARACTER
	FieldLayout<color32>(),      // FIELD_COLOR32
	{},                         // FIELD_EMBEDDED (type-specific storage)
	{},                         // FIELD_CUSTOM (type-specific storage)
	FieldLayout<CBaseEntity*>(), // FIELD_CLASSPTR
	FieldLayout<CBaseHandle>(),  // FIELD_EHANDLE
	FieldLayout<edict_t*>(),     // FIELD_EDICT
	FieldLayout<Vector>(),       // FIELD_POSITION_VECTOR
	FieldLayout<float>(),        // FIELD_TIME
	FieldLayout<int>(),          // FIELD_TICK
	FieldLayout<string_t>(),     // FIELD_MODELNAME
	FieldLayout<string_t>(),     // FIELD_SOUNDNAME
	{},                         // FIELD_INPUT (no storage allocated by CBaseNPC)
	FieldLayout<inputfunc_t>(),  // FIELD_FUNCTION
	FieldLayout<VMatrix>(),      // FIELD_VMATRIX
	FieldLayout<VMatrix>(),      // FIELD_VMATRIX_WORLDSPACE
	FieldLayout<matrix3x4_t>(),  // FIELD_MATRIX3X4_WORLDSPACE
	FieldLayout<interval_t>(),   // FIELD_INTERVAL
	FieldLayout<int>(),          // FIELD_MODELINDEX
	FieldLayout<int>(),          // FIELD_MATERIALINDEX
	FieldLayout<Vector2D>(),     // FIELD_VECTOR2D
};

static_assert(sizeof(g_DataMapFieldLayouts) / sizeof(g_DataMapFieldLayouts[0]) == FIELD_TYPECOUNT);
static_assert(sizeof(color32) == sizeof(int));
}

size_t GetAlignedOffset( size_t offset, size_t align, size_t* padding = nullptr )
{
	if ( align == 0 ) return offset;

	size_t pad = ~( offset - 1 ) & ( align - 1 );
	if ( padding ) *padding = pad;

	return offset + pad;
}

void IDataMapContainer::EndDataDesc()
{
	m_bHasDataMapDesc = true;

	if (m_vecEntityDataTypeDescriptors.Count() == 0)
	{
		// For "empty" tables
		m_vecEntityDataTypeDescriptors.AddToTail( { FIELD_VOID, 0, {0,0}, 0, 0, 0, 0, 0, 0 } );
	}

	const size_t startOffset = GetDataDescOffset();
	m_DataMapDescSizeInBytes = GetAlignedOffset(startOffset + m_DataMapDescSizeInBytes, alignof(void*)) - startOffset;
}

void IDataMapContainer::DefineField(const char* name, fieldtype_t fieldType, unsigned short count, short flags, const char* externalName, float fieldTolerance)
{
	size_t padding = 0;
	const auto& layout = g_DataMapFieldLayouts[fieldType];
	int fieldOffset = GetAlignedOffset( GetDataDescOffset() + m_DataMapDescSizeInBytes, layout.alignment, &padding );
	int fieldSizeInBytes = layout.size * count;

	name = name ? strdup(name) : NULL;
	externalName = externalName ? strdup(externalName) : NULL;

	m_vecEntityDataTypeDescriptors.AddToTail({ 
		fieldType, 
		name, 
		{ fieldOffset, 0 }, 
		count,
		flags,
		externalName,
		NULL, NULL, NULL,
		fieldSizeInBytes,
		NULL,
		0,
		fieldTolerance
	});

	m_DataMapDescSizeInBytes += padding + fieldSizeInBytes;
}

void IDataMapContainer::InitializeEntityHandles(void* object) const
{
	if (!object)
		return;

	for (int i = 0; i < m_vecEntityDataTypeDescriptors.Count(); ++i)
	{
		const auto& field = m_vecEntityDataTypeDescriptors[i];
		if (field.fieldType != FIELD_EHANDLE || (field.flags & FTYPEDESC_INPUT))
			continue;

		auto* storage = static_cast<uint8_t*>(object) + field.fieldOffset[TD_OFFSET_NORMAL];
		for (unsigned int element = 0; element < field.fieldSize; ++element)
			new (storage + element * sizeof(CBaseHandle)) CBaseHandle();
	}
}

// from sourcemod/public/compat_wrappers.h
// NOTE: Adjust if expanding to other games
inline int GetTypeDescOffs(typedescription_t *td)
{
	return td->fieldOffset[TD_OFFSET_NORMAL];
}

// from sourcemod/core/HalfLife2.cpp
bool UTIL_FindDataMapInfo(datamap_t *pMap, const char *name, sm_datatable_info_t *pDataTable)
{
	while (pMap)
	{
		for (int i = 0; i < pMap->dataNumFields; ++i)
		{
			if (pMap->dataDesc[i].fieldName == NULL)
			{
				continue;
			}
			if (strcmp(name, pMap->dataDesc[i].fieldName) == 0)
			{
				pDataTable->prop = &(pMap->dataDesc[i]);
				pDataTable->actual_offset = GetTypeDescOffs(pDataTable->prop);
				return true;
			}
			if (pMap->dataDesc[i].td == NULL || !UTIL_FindDataMapInfo(pMap->dataDesc[i].td, name, pDataTable))
			{
				continue;
			}
			
			pDataTable->actual_offset += GetTypeDescOffs(&(pMap->dataDesc[i]));
			return true;
		}
		
		pMap = pMap->baseMap;
	}

	return false; 
}

bool IDataMapContainer::FindDataMapInfo(const char* name, sm_datatable_info_t *pDataTable, char* error, size_t maxlen)
{
	datamap_t* pDataMap = GetDataDescMap();
	if (!pDataMap)
	{
		if (error) snprintf( error, maxlen, "Could not retrieve datamap" );

		return false;
	}

	// Search in cache first.
	unsigned short index = m_DataMapCache.Find( name );
	if (m_DataMapCache.IsValidIndex(index))
	{
		*pDataTable = m_DataMapCache[index];
		return true;
	}

	if (UTIL_FindDataMapInfo( pDataMap, name, pDataTable ))
	{
		m_DataMapCache.Insert(name, *pDataTable);
		return true;
	}

	if (error) snprintf( error, maxlen, "Property \"%s\" not found", name );

	return false;
}

bool IDataMapContainer::GetDataOffset( const sm_datatable_info_t &info, int &offset, int element, char* error, size_t maxlen) const
{
	typedescription_t* td = info.prop;

	if (element < 0 || element >= td->fieldSize)
	{
		if (error) snprintf( error, maxlen, "Element %d is out of bounds (Prop %s has %d elements).",
			element, td->fieldName, td->fieldSize );

		return false;
	}
	
	offset = info.actual_offset + (element * (td->fieldSizeInBytes / td->fieldSize));
	return true;
}

bool IDataMapContainer::GetObjectData( void* obj, const char* prop, int &data, int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
		return false;
	
	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
		return false;
	
	switch ( info.prop->fieldType )
	{
		case FIELD_TICK:
		case FIELD_MODELINDEX:
		case FIELD_MATERIALINDEX:
		case FIELD_INTEGER:
		case FIELD_COLOR32:
			std::memcpy(&data, (int8_t*)obj + offset, sizeof(data));
			return true;
		case FIELD_CHARACTER:
			data = *((int8_t*)obj + offset);
			return true;
		case FIELD_BOOLEAN:
			data = *(bool*)((int8_t*)obj + offset);
			return true;
		case FIELD_SHORT:
			data = *(int16_t*)((int8_t*)obj + offset);
			return true;
	}

	if (error) snprintf( error, maxlen, "Data field %s is not an integer (%d)", prop, info.prop->fieldType );
	return false;
}

bool IDataMapContainer::SetObjectData( void* obj, const char* prop, int data, int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
		return false;
	
	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
		return false;
	
	switch ( info.prop->fieldType )
	{
		case FIELD_TICK:
		case FIELD_MODELINDEX:
		case FIELD_MATERIALINDEX:
		case FIELD_INTEGER:
		case FIELD_COLOR32:
			std::memcpy((int8_t*)obj + offset, &data, sizeof(data));
			return true;
		case FIELD_CHARACTER:
			*((int8_t*)obj + offset) = (int8_t)data;
			return true;
		case FIELD_BOOLEAN:
			*(bool*)((int8_t*)obj + offset) = !!data;
			return true;
		case FIELD_SHORT:
			*(int16_t*)((int8_t*)obj + offset) = (int16_t)data;
			return true;
	}

	if (error) snprintf( error, maxlen, "Data field %s is not an integer (%d)", prop, info.prop->fieldType );
	return false;
}

bool IDataMapContainer::GetObjectDataFloat( void* obj, const char* prop, float &data, int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
		return false;
	
	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
		return false;

	switch ( info.prop->fieldType )
	{
		case FIELD_FLOAT:
		case FIELD_TIME:
			data = *(float*)((int8_t*)obj + offset);
			return true;
	}

	if (error) snprintf( error, maxlen, "Data field %s is not a float (%d != [%d,%d])", prop, info.prop->fieldType, FIELD_FLOAT, FIELD_TIME );
	return false;
}

bool IDataMapContainer::SetObjectDataFloat( void* obj, const char* prop, float data, int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
		return false;
	
	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
		return false;
	
	switch ( info.prop->fieldType )
	{
		case FIELD_FLOAT:
		case FIELD_TIME:
			*(float*)((int8_t*)obj + offset) = data;
			return true;
	}

	if (error) snprintf( error, maxlen, "Data field %s is not a float (%d != [%d,%d])", prop, info.prop->fieldType, FIELD_FLOAT, FIELD_TIME );
	return false;
}

bool IDataMapContainer::GetObjectDataVector( void* obj, const char* prop, float data[3], int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
		return false;
	
	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
		return false;

	switch ( info.prop->fieldType )
	{
		case FIELD_VECTOR:
		case FIELD_POSITION_VECTOR:
			float* src = (float*)((int8_t*)obj + offset);
			data[0] = src[0]; data[1] = src[1]; data[2] = src[2]; 
			return true;
	}

	if (error) snprintf( error, maxlen, "Data field %s is not a vector (%d != [%d,%d])", prop, info.prop->fieldType, FIELD_VECTOR, FIELD_POSITION_VECTOR );
	return false;
}

bool IDataMapContainer::SetObjectDataVector( void* obj, const char* prop, const float data[3], int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
		return false;
	
	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
		return false;

	switch ( info.prop->fieldType )
	{
		case FIELD_VECTOR:
		case FIELD_POSITION_VECTOR:
			float* dest = (float*)((int8_t*)obj + offset);
			dest[0] = data[0]; dest[1] = data[1]; dest[2] = data[2]; 
			return true;
	}

	if (error) snprintf( error, maxlen, "Data field %s is not a vector (%d != [%d,%d])", prop, info.prop->fieldType, FIELD_VECTOR, FIELD_POSITION_VECTOR );
	return false;
}

bool IDataMapContainer::GetObjectDataString( void* obj, const char* prop, char* data, size_t datamaxlen, int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
		return false;
	
	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
		return false;
	
	bool bIsStringIndex = (info.prop->fieldType != FIELD_CHARACTER);

	switch ( info.prop->fieldType )
	{
		case FIELD_CHARACTER:
		{
			if (element != 0)
			{
				if (error) snprintf(error, maxlen, "Prop %s is not an array. Element %d is invalid.", prop, element);
				return false;
			}

			char* src = (char *)((uint8_t *)obj + offset);
			ke::SafeStrcpyN(data, datamaxlen, src, info.prop->fieldSize );

			return true;
		}
		case FIELD_STRING:
		case FIELD_MODELNAME:
		case FIELD_SOUNDNAME:
		{
			string_t idx = *(string_t *)((uint8_t *)obj + offset);
			const char* src = (idx == NULL_STRING) ? "" : STRING(idx);
			ke::SafeStrcpy(data, datamaxlen, src);

			return true;
		}
	}

	if (error) snprintf(error, maxlen, "Data field %s is not a string (%d != %d)", prop, info.prop->fieldType, FIELD_CHARACTER);
	return false;
}

bool IDataMapContainer::SetObjectDataString( void* obj, const char* prop, const char* data, int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
		return false;
	
	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
		return false;
	
	bool bIsStringIndex = (info.prop->fieldType != FIELD_CHARACTER);

	switch ( info.prop->fieldType )
	{
		case FIELD_CHARACTER:
		{
			if (element != 0)
			{
				if (error) snprintf(error, maxlen, "Prop %s is not an array. Element %d is invalid.", prop, element);
				return false;
			}

			char* dest = (char *)((uint8_t *)obj + offset);
			ke::SafeStrcpy(dest, info.prop->fieldSize, data);

			return true;
		}
		case FIELD_STRING:
		case FIELD_MODELNAME:
		case FIELD_SOUNDNAME:
		{
			*(string_t *)((uint8_t *)obj + offset) = AllocPooledString( data );
			return true;
		}
	}

	if (error) snprintf(error, maxlen, "Data field %s is not a string (%d != %d)", prop, info.prop->fieldType, FIELD_CHARACTER);
	return false;
}

bool IDataMapContainer::GetObjectDataEntity( void* obj, const char* prop, CBaseEntity** data, int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
	{
		return false;
	}

	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
	{
		return false;
	}

	switch (info.prop->fieldType)
	{
		case FIELD_EHANDLE:
		case FIELD_CUSTOM:
		{
			CBaseHandle* hndl;

			if (info.prop->fieldType == FIELD_EHANDLE)
			{
				hndl = (CBaseHandle *)((uint8_t *)obj + offset);
			}
			else if (info.prop->fieldType == FIELD_CUSTOM && (info.prop->flags & FTYPEDESC_OUTPUT) == FTYPEDESC_OUTPUT)
			{
				auto *pVariant = (variant_t *)((intptr_t)obj + offset);
				hndl = &pVariant->eVal;
			}
			else
			{
				break;
			}
			 
			CBaseEntity* pHandleEntity = gamehelpers->ReferenceToEntity(hndl->GetEntryIndex());

			if (pHandleEntity && *hndl == reinterpret_cast<IHandleEntity *>(pHandleEntity)->GetRefEHandle())
			{
				*data = pHandleEntity;
			}
			else
			{
				*data = nullptr;
			}

			return true;
		}
		case FIELD_CLASSPTR:
		{
			*data = *(CBaseEntity **)((uint8_t *)obj + offset);
			return true;
		}
		case FIELD_EDICT:
		{
			edict_t *pEdict = *(edict_t **)((uint8_t *)obj + offset);
			if (pEdict && !pEdict->IsFree())
			{
				*data = gamehelpers->ReferenceToEntity(gamehelpers->IndexOfEdict(pEdict));
			}
			else
			{
				*data = nullptr;
			}

			return true;
		}
	}

	if (error)
	{
		snprintf(error, maxlen, "Data field %s is not an entity nor edict (%d)", prop, info.prop->fieldType);
	}

	return false;
}

bool IDataMapContainer::SetObjectDataEntity( void* obj, const char* prop, CBaseEntity* data, int element, char* error, size_t maxlen )
{
	sm_datatable_info_t info;
	if (!FindDataMapInfo(prop, &info, error, maxlen))
	{
		return false;
	}

	int offset;
	if (!GetDataOffset(info, offset, element, error, maxlen))
	{
		return false;
	}

	switch (info.prop->fieldType)
	{
		case FIELD_EHANDLE:
		case FIELD_CUSTOM:
		{
			CBaseHandle* hndl;

			if (info.prop->fieldType == FIELD_EHANDLE)
			{
				hndl = (CBaseHandle *)((uint8_t *)obj + offset);
			}
			else if (info.prop->fieldType == FIELD_CUSTOM && (info.prop->flags & FTYPEDESC_OUTPUT) == FTYPEDESC_OUTPUT)
			{
				auto *pVariant = (variant_t *)((intptr_t)obj + offset);
				hndl = &pVariant->eVal;
			}
			else
			{
				break;
			}

			hndl->Set((IHandleEntity *)data);
			return true;
		}
		case FIELD_CLASSPTR:
		{
			*(CBaseEntity **)((uint8_t *)obj + offset) = data;
			return true;
		}
		case FIELD_EDICT:
		{
			edict_t *pEdict = nullptr;

			if (data)
			{
				IServerNetworkable *pNetworkable = ((IServerUnknown *)data)->GetNetworkable();
				if (!pNetworkable)
				{
					if (error)
					{
						snprintf(error, maxlen, "Entity does not have a valid edict");
					}
					
					return false;
				}

				pEdict = pNetworkable->GetEdict();
				if (!pEdict || pEdict->IsFree())
				{
					if (error)
					{
						snprintf(error, maxlen, "Entity does not have a valid edict");
					}

					return false;
				}
			}

			*(edict_t **)((uint8_t *)obj + offset) = pEdict;
			return true;
		}
	}

	if (error)
	{
		snprintf(error, maxlen, "Data field %s is not an entity nor edict (%d)", prop, info.prop->fieldType);
	}

	return false;
}

template<>
bool IDataMapContainer::SetObjectDataEx(void* obj, const char* prop, int data, int element, char* error, size_t maxlen) {
	return this->SetObjectData(obj, prop, data, element, error, maxlen);
}

template<>
bool IDataMapContainer::SetObjectDataEx(void* obj, const char* prop, float data, int element, char* error, size_t maxlen) {
	return this->SetObjectDataFloat(obj, prop, data, element, error, maxlen);
}

template<>
bool IDataMapContainer::SetObjectDataEx(void* obj, const char* prop, float* data, int element, char* error, size_t maxlen) {
	return this->SetObjectDataVector(obj, prop, data, element, error, maxlen);
}

template<>
bool IDataMapContainer::SetObjectDataEx(void* obj, const char* prop, CBaseEntity* data, int element, char* error, size_t maxlen) {
	return this->SetObjectDataEntity(obj, prop, data, element, error, maxlen);
}

template<>
bool IDataMapContainer::SetObjectDataEx(void* obj, const char* prop, char* data, int element, char* error, size_t maxlen) {
	return this->SetObjectDataString(obj, prop, data, element, error, maxlen);
}

template<>
bool IDataMapContainer::SetObjectDataEx(void* obj, const char* prop, const char* data, int element, char* error, size_t maxlen) {
	return this->SetObjectDataString(obj, prop, data, element, error, maxlen);
}

template<>
bool IDataMapContainer::GetObjectDataEx(void* obj, const char* prop, int* data, int element, char* error, size_t maxlen) {
	return this->GetObjectData(obj, prop, *data, element, error, maxlen);
}

template<>
bool IDataMapContainer::GetObjectDataEx(void* obj, const char* prop, float* data, int element, char* error, size_t maxlen) {
	return this->GetObjectDataFloat(obj, prop, *data, element, error, maxlen);
}

template<>
bool IDataMapContainer::GetObjectDataEx(void* obj, const char* prop, float** data, int element, char* error, size_t maxlen) {
	return this->GetObjectDataVector(obj, prop, *data, element, error, maxlen);
}

template<>
bool IDataMapContainer::GetObjectDataEx(void* obj, const char* prop, CBaseEntity** data, int element, char* error, size_t maxlen) {
	return this->GetObjectDataEntity(obj, prop, data, element, error, maxlen);
}

template<>
bool IDataMapContainer::GetObjectDataEx(void* obj, const char* prop, char** data, int element, char* error, size_t maxlen) {
	*data = new char[256];
	if (this->GetObjectDataString(obj, prop, *data, 256, element, error, maxlen)) {
		return true;
	}
	delete *data;
	*data = nullptr;
	return false;
}

namespace
{
struct EntityInputCallInterface
{
	ffi_cif cif;
	ffi_type* args[2] = { &ffi_type_pointer, &ffi_type_pointer };

	// The engine passes the receiver and the address of the inputdata_t reference.
#if defined(_WIN32) && defined(_M_IX86)
	static constexpr ffi_abi abi = FFI_THISCALL;
#else
	static constexpr ffi_abi abi = FFI_DEFAULT_ABI;
#endif
	const ffi_status status = ffi_prep_cif(&cif, abi, 2, &ffi_type_void, args);
};
}

bool IEntityDataMapInputFuncDelegate::Prepare(inputfunc_t& inputFunc)
{
	static EntityInputCallInterface call;
	if (call.status != FFI_OK)
		return false;

	void* code = nullptr;
	m_pClosure = static_cast<ffi_closure*>(ffi_closure_alloc(sizeof(ffi_closure), &code));
	if (!m_pClosure || ffi_prep_closure_loc(m_pClosure, &call.cif, HandleInput, this, code) != FFI_OK)
		return false;

#ifdef _MSC_VER
	// CBaseEntity uses the MSVC single-inheritance member pointer layout.
	static_assert(sizeof(inputFunc) == sizeof(code));
	std::memcpy(&inputFunc, &code, sizeof(inputFunc));
#else
	// The Itanium ABI also stores the adjustment to apply to the receiver.
	const struct { void* code; intptr_t adjustment; } member = { code, 0 };
	static_assert(sizeof(inputFunc) == sizeof(member));
	std::memcpy(&inputFunc, &member, sizeof(inputFunc));
#endif
	return true;
}

void IEntityDataMapInputFuncDelegate::HandleInput(ffi_cif*, void*, void** args, void* userData)
{
	auto* delegate = static_cast<IEntityDataMapInputFuncDelegate*>(userData);
	auto* entity = *static_cast<CBaseEntity**>(args[0]);
	auto* data = *static_cast<inputdata_t**>(args[1]);
	delegate->OnInput(entity, *data);
}

IEntityDataMapInputFuncDelegate::~IEntityDataMapInputFuncDelegate()
{
	if (m_pClosure)
		ffi_closure_free(m_pClosure);
}

void IEntityDataMapContainer::DestroyDataDesc()
{
	IDataMapContainer::DestroyDataDesc();

	m_pEntityInputFuncDelegates.PurgeAndDeleteElements();
}

void IEntityDataMapContainer::DestroyDataDescMap()
{
	if (m_pDataMap)
		gamehelpers->RemoveDataTableCache(m_pDataMap);

	IDataMapContainer::DestroyDataDescMap();
}

void IEntityDataMapContainer::DefineField(const char* name, fieldtype_t fieldType, unsigned short count)
{
	DefineField(name, fieldType, count, FTYPEDESC_SAVE, NULL, 0);
}

void IEntityDataMapContainer::DefineKeyField(const char* name, fieldtype_t fieldType, const char* mapname)
{
	DefineField(name, fieldType, 1, FTYPEDESC_KEY | FTYPEDESC_SAVE, strdup(mapname), 0);
}

bool IEntityDataMapContainer::DefineInputFunc(const char* name, fieldtype_t fieldType, const char* mapname, IEntityDataMapInputFuncDelegate* pDelegate)
{
	std::unique_ptr<IEntityDataMapInputFuncDelegate> delegate(pDelegate);
	inputfunc_t inputFunc;
	if (!delegate->Prepare(inputFunc))
		return false;

	name = name ? strdup(name) : NULL;
	mapname = mapname ? strdup(mapname) : NULL;

	typedescription_t typeDesc = { 
		fieldType, 
		name, 
		{ 0, 0 }, 
		1,
		FTYPEDESC_INPUT,
		mapname,
		NULL, 
		inputFunc, NULL,
		0,
		NULL,
		0,
		0
	};

	m_pEntityInputFuncDelegates.AddToTail(delegate.release());
	m_vecEntityDataTypeDescriptors.AddToTail(typeDesc);
	return true;
}

void IEntityDataMapContainer::DefineOutput(const char* name, const char* mapname)
{
	size_t padding = 0;
	int fieldOffset = GetAlignedOffset( GetDataDescOffset() + m_DataMapDescSizeInBytes, alignof(CBaseEntityOutput), &padding );
	int fieldSizeInBytes = sizeof(CBaseEntityOutput);

	name = name ? strdup(name) : NULL;
	mapname = mapname ? strdup(mapname) : NULL;

	m_vecEntityDataTypeDescriptors.AddToTail({ 
		FIELD_CUSTOM, 
		name, 
		{ fieldOffset, 0 }, 
		1,
		FTYPEDESC_OUTPUT | FTYPEDESC_SAVE | FTYPEDESC_KEY,
		mapname,
		eventFuncs, 
		NULL, NULL,
		0,
		NULL,
		0,
		0
	});

	m_DataMapDescSizeInBytes += padding + fieldSizeInBytes;
}

void IEntityDataMapContainer::CreateFields(CBaseEntity* pEntity)
{
	if (!m_pDataMap)
		return;

	InitializeEntityHandles(pEntity);
	
	for (int i = 0; i < m_pDataMap->dataNumFields; i++)
	{
		typedescription_t * pTypeDesc = &m_pDataMap->dataDesc[i];
		if ( ( pTypeDesc->fieldType == FIELD_CUSTOM ) && ( pTypeDesc->flags & FTYPEDESC_OUTPUT ) )
		{
			CBaseEntityOutput *pOutput = (CBaseEntityOutput *)((uint8_t*)pEntity + pTypeDesc->fieldOffset[0]);
			pOutput->Init();
		}
	}
}

void IEntityDataMapContainer::DestroyFields(CBaseEntity* pEntity)
{
	if (!m_pDataMap)
		return;

	for (int i = 0; i < m_pDataMap->dataNumFields; i++)
	{
		typedescription_t * pTypeDesc = &m_pDataMap->dataDesc[i];
		if ( ( pTypeDesc->fieldType == FIELD_CUSTOM ) && ( pTypeDesc->flags & FTYPEDESC_OUTPUT ) )
		{
			CBaseEntityOutput *pOutput = (CBaseEntityOutput *)((uint8_t*)pEntity + pTypeDesc->fieldOffset[0]);
			pOutput->Destroy();
		}
	}
}
