/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "System/Platform/CpuTopology.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <sys/sysctl.h>

namespace cpu_topology {
namespace {

constexpr unsigned int MAX_SUPPORTED_CPUS = 32;
constexpr unsigned int MAX_PERFORMANCE_LEVELS = 2;

struct PerformanceLevel {
	unsigned int logicalCpuCount = 0;
	unsigned int physicalCpuCount = 0;
	unsigned int cpusPerL2 = 0;
	unsigned int cpusPerL3 = 0;
	uint32_t l2CacheSize = 0;
	uint32_t l3CacheSize = 0;
};

int ReadSysctlInt(const std::string& name)
{
	int value = 0;
	size_t valueSize = sizeof(value);

	if (sysctlbyname(name.c_str(), &value, &valueSize, nullptr, 0) != 0)
		return 0;

	return std::max(value, 0);
}

uint32_t ReadSysctlCacheSize(const std::string& name)
{
	const int value = ReadSysctlInt(name);
	return static_cast<uint32_t>(value);
}

uint32_t ReadSysctlCacheSize64(const char* name)
{
	uint64_t value = 0;
	size_t valueSize = sizeof(value);

	if (sysctlbyname(name, &value, &valueSize, nullptr, 0) != 0)
		return 0;

	return static_cast<uint32_t>(std::min<uint64_t>(value, std::numeric_limits<uint32_t>::max()));
}

uint32_t MakeCpuMask(unsigned int firstCpu, unsigned int cpuCount)
{
	uint32_t mask = 0;
	const unsigned int lastCpu = std::min(firstCpu + cpuCount, MAX_SUPPORTED_CPUS);

	for (unsigned int cpu = firstCpu; cpu < lastCpu; ++cpu)
		mask |= (uint32_t{1} << cpu);

	return mask;
}

unsigned int GetLogicalCpuCount()
{
	int count = ReadSysctlInt("hw.logicalcpu");

	if (count == 0)
		count = ReadSysctlInt("hw.ncpu");

	return std::clamp<unsigned int>(count, 1, MAX_SUPPORTED_CPUS);
}

std::vector<PerformanceLevel> GetPerformanceLevels()
{
	const unsigned int levelCount = std::min<unsigned int>(
		ReadSysctlInt("hw.nperflevels"),
		MAX_PERFORMANCE_LEVELS
	);
	std::vector<PerformanceLevel> levels;
	levels.reserve(levelCount);

	for (unsigned int level = 0; level < levelCount; ++level) {
		const std::string prefix = "hw.perflevel" + std::to_string(level) + ".";
		PerformanceLevel info;
		info.logicalCpuCount = ReadSysctlInt(prefix + "logicalcpu");

		if (info.logicalCpuCount == 0)
			continue;

		info.physicalCpuCount = ReadSysctlInt(prefix + "physicalcpu");
		if (info.physicalCpuCount == 0)
			info.physicalCpuCount = info.logicalCpuCount;

		info.physicalCpuCount = std::min(info.physicalCpuCount, info.logicalCpuCount);
		info.cpusPerL2 = ReadSysctlInt(prefix + "cpusperl2");
		info.cpusPerL3 = ReadSysctlInt(prefix + "cpusperl3");
		info.l2CacheSize = ReadSysctlCacheSize(prefix + "l2cachesize");
		info.l3CacheSize = ReadSysctlCacheSize(prefix + "l3cachesize");
		levels.push_back(info);
	}

	return levels;
}

void AddProcessorLevelMask(
	ProcessorMasks& masks,
	const PerformanceLevel& level,
	unsigned int& firstCpu,
	bool performance
) {
	const unsigned int logicalCount = std::min(level.logicalCpuCount, MAX_SUPPORTED_CPUS - firstCpu);
	const unsigned int physicalCount = std::min(level.physicalCpuCount, logicalCount);
	const uint32_t levelMask = MakeCpuMask(firstCpu, logicalCount);

	if (performance)
		masks.performanceCoreMask |= levelMask;
	else
		masks.efficiencyCoreMask |= levelMask;

	const unsigned int smtCount = logicalCount - physicalCount;
	masks.hyperThreadLowMask |= MakeCpuMask(firstCpu, std::min(smtCount, physicalCount));
	masks.hyperThreadHighMask |= MakeCpuMask(firstCpu + physicalCount, smtCount);
	firstCpu += logicalCount;
}

void AddCacheGroups(
	ProcessorCaches& caches,
	const PerformanceLevel& level,
	unsigned int& firstCpu
) {
	const unsigned int logicalCount = std::min(level.logicalCpuCount, MAX_SUPPORTED_CPUS - firstCpu);
	unsigned int cpusPerCache = level.cpusPerL3;

	if (cpusPerCache == 0)
		cpusPerCache = level.cpusPerL2;
	if (cpusPerCache == 0)
		cpusPerCache = logicalCount;

	for (unsigned int offset = 0; offset < logicalCount; offset += cpusPerCache) {
		ProcessorGroupCaches group;
		group.groupMask = MakeCpuMask(firstCpu + offset, std::min(cpusPerCache, logicalCount - offset));
		group.cacheSizes[1] = level.l2CacheSize;
		group.cacheSizes[2] = level.l3CacheSize;
		caches.groupCaches.push_back(group);
	}

	firstCpu += logicalCount;
}

} // namespace

ProcessorMasks GetProcessorMasks()
{
	ProcessorMasks masks;
	const auto levels = GetPerformanceLevels();
	unsigned int firstCpu = 0;

	for (size_t level = 0; level < levels.size() && firstCpu < MAX_SUPPORTED_CPUS; ++level)
		AddProcessorLevelMask(masks, levels[level], firstCpu, level == 0);

	const unsigned int logicalCpuCount = GetLogicalCpuCount();
	if (firstCpu < logicalCpuCount) {
		PerformanceLevel fallback;
		fallback.logicalCpuCount = logicalCpuCount - firstCpu;
		fallback.physicalCpuCount = std::min<unsigned int>(
			ReadSysctlInt("hw.physicalcpu"),
			fallback.logicalCpuCount
		);
		if (fallback.physicalCpuCount == 0)
			fallback.physicalCpuCount = fallback.logicalCpuCount;

		AddProcessorLevelMask(masks, fallback, firstCpu, levels.empty());
	}

	return masks;
}

ProcessorCaches GetProcessorCache()
{
	ProcessorCaches caches;
	const auto levels = GetPerformanceLevels();
	unsigned int firstCpu = 0;

	for (const PerformanceLevel& level: levels) {
		if (firstCpu >= MAX_SUPPORTED_CPUS)
			break;

		AddCacheGroups(caches, level, firstCpu);
	}

	const unsigned int logicalCpuCount = GetLogicalCpuCount();
	if (firstCpu < logicalCpuCount) {
		ProcessorGroupCaches fallback;
		fallback.groupMask = MakeCpuMask(firstCpu, logicalCpuCount - firstCpu);
		fallback.cacheSizes[1] = ReadSysctlCacheSize64("hw.l2cachesize");
		fallback.cacheSizes[2] = ReadSysctlCacheSize64("hw.l3cachesize");
		caches.groupCaches.push_back(fallback);
	}

	return caches;
}

ThreadPinPolicy GetThreadPinPolicy()
{
	return THREAD_PIN_POLICY_NONE;
}

} // namespace cpu_topology
