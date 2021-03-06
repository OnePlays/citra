// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "common/log.h"
#include "core/hle/hle.h"
#include "core/hle/service/err_f.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Namespace ERR_F

namespace ERR_F {

    const Interface::FunctionInfo FunctionTable[] = {
        {0x00010800, nullptr,               "ThrowFatalError"}
    };
    ////////////////////////////////////////////////////////////////////////////////////////////////////
    // Interface class

    Interface::Interface() {
        Register(FunctionTable, ARRAY_SIZE(FunctionTable));
    }

    Interface::~Interface() {
    }

} // namespace
