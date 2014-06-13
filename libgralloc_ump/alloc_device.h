/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <hardware/hardware.h>

#ifndef AWAR
#define AWAR(fmt, args...) __android_log_print(ANDROID_LOG_WARN, "[Gralloc-3X-Warning]", "%s:%d "fmt,__func__,__LINE__,args)
#endif
#ifndef AINF
#define AINF(fmt, args...) __android_log_print(ANDROID_LOG_INFO, "[Gralloc-3X]", fmt,args)
#endif
#ifndef AERR
#define AERR(fmt, args...) __android_log_print(ANDROID_LOG_ERROR, "[Gralloc-3X-ERROR]", "%s:%d "fmt,__func__,__LINE__,args)
#endif
#ifndef AERR_IF
#define AERR_IF( eq, fmt, args...) if ( (eq) ) AERR( fmt, args )
#endif

// Create an alloc device
int alloc_device_open(hw_module_t const* module, const char* name, hw_device_t** device);
