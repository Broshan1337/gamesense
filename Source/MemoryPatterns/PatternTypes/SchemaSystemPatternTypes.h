#pragma once

#include <cstdint>

#include <Utils/StrongTypeAlias.h>

// Internal (non-exported) libschemasystem.so functions. Signatures reverse engineered
// against a specific build - see SchemaSystem class for usage. All operate on opaque
// engine-owned objects; this codebase only ever forwards the pointers it gets back.

using SchemaFindDeclaredClassOrEnumFn = void*(void* typeScope, const char* name);
using SchemaBeginFieldIteratorFn = void(void* outIterator, void* classBinding, std::int32_t kind);
using SchemaFieldIteratorHasNextFn = bool(void* iterator);
using SchemaFieldIteratorCurrentFn = void*(void* iterator);
using SchemaFieldIteratorNextFn = void(void* iterator);
using SchemaFieldIteratorOffsetFn = std::int32_t(void* iterator);

STRONG_TYPE_ALIAS(GlobalTypeScopePointer, void*);
STRONG_TYPE_ALIAS(PointerToSchemaFindDeclaredClassOrEnum, SchemaFindDeclaredClassOrEnumFn*);
STRONG_TYPE_ALIAS(PointerToSchemaBeginFieldIterator, SchemaBeginFieldIteratorFn*);
STRONG_TYPE_ALIAS(PointerToSchemaFieldIteratorHasNext, SchemaFieldIteratorHasNextFn*);
STRONG_TYPE_ALIAS(PointerToSchemaFieldIteratorCurrent, SchemaFieldIteratorCurrentFn*);
STRONG_TYPE_ALIAS(PointerToSchemaFieldIteratorNext, SchemaFieldIteratorNextFn*);
STRONG_TYPE_ALIAS(PointerToSchemaFieldIteratorOffset, SchemaFieldIteratorOffsetFn*);
