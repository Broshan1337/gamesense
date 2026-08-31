#pragma once

namespace cs2
{

// Only the leading field this codebase actually reads is modeled - reverse engineered
// from libschemasystem.so, see SchemaSystem for how it is used.
struct CSchemaClassFieldData {
    void* unknown0;
    const char* name;
};

}
