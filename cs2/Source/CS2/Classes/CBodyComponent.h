#pragma once

#include "CModelState.h"

namespace cs2
{

struct CBodyComponent {};






struct CSkeletonInstance {
    using m_modelState = CModelState;
};

struct CBodyComponentSkeletonInstance {
    using m_skeletonInstance = CSkeletonInstance;
};

}
