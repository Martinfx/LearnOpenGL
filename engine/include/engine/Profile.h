#pragma once
// Profiling zones for the Tracy profiler (3rdparty/tracy). Build with -DENGINE_TRACY=ON, run the
// game or the editor and connect the Tracy UI: a timeline of every thread (main, job workers,
// audio streaming, loaders) with these zones, GPU zones of the render passes and frame marks.
// Without ENGINE_TRACY every macro is empty and costs nothing.
#include <tracy/Tracy.hpp>

#include <cstring>

#define ENGINE_ZONE(name) ZoneScopedN(name)
#define ENGINE_FUNCTION_ZONE ZoneScoped
// A zone whose name is only known at run time (task graph tasks).
#define ENGINE_ZONE_DYNAMIC(name)                                                                                      \
    ZoneScopedN("task");                                                                                               \
    ZoneName(name, std::strlen(name))
#define ENGINE_FRAME_MARK FrameMark
#define ENGINE_PLOT(name, value) TracyPlot(name, value)
#ifdef TRACY_ENABLE
#define ENGINE_THREAD_NAME(name) tracy::SetThreadName(name)
#else
#define ENGINE_THREAD_NAME(name) ((void)0)
#endif
