/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <catch_amalgamated.hpp>

#include "Lua/LuaOpenGLCoreBatch.h"

using LuaOpenGLCore::PrimitiveAssembler;
using LuaOpenGLCore::PrimitiveMode;

TEST_CASE("LuaOpenGLCoreBatch converts legacy quads")
{
	PrimitiveAssembler<int> assembler(PrimitiveMode::Quads);
	for (int vertex = 0; vertex < 8; ++vertex)
		assembler.AddVertex(vertex);

	const auto batch = assembler.Flush(true);

	CHECK(batch.mode == PrimitiveMode::Triangles);
	CHECK(batch.vertices == std::vector<int> {
		0, 1, 2, 0, 2, 3,
		4, 5, 6, 4, 6, 7,
	});
	CHECK(batch.discardedVertices == 0);
}

TEST_CASE("LuaOpenGLCoreBatch preserves nested draw order")
{
	PrimitiveAssembler<int> outer(PrimitiveMode::Quads);
	for (int vertex = 0; vertex < 4; ++vertex)
		outer.AddVertex(vertex);

	const auto outerPrefix = outer.Flush(false);

	PrimitiveAssembler<int> inner(PrimitiveMode::Triangles);
	inner.AddVertex(10);
	inner.AddVertex(11);
	inner.AddVertex(12);
	const auto innerBatch = inner.Flush(true);

	for (int vertex = 4; vertex < 8; ++vertex)
		outer.AddVertex(vertex);
	const auto outerSuffix = outer.Flush(true);

	std::vector<int> drawOrder;
	drawOrder.insert(drawOrder.end(), outerPrefix.vertices.begin(), outerPrefix.vertices.end());
	drawOrder.insert(drawOrder.end(), innerBatch.vertices.begin(), innerBatch.vertices.end());
	drawOrder.insert(drawOrder.end(), outerSuffix.vertices.begin(), outerSuffix.vertices.end());

	CHECK(drawOrder == std::vector<int> {
		0, 1, 2, 0, 2, 3,
		10, 11, 12,
		4, 5, 6, 4, 6, 7,
	});
}

TEST_CASE("LuaOpenGLCoreBatch retains incomplete outer primitives")
{
	PrimitiveAssembler<int> outer(PrimitiveMode::Quads);
	outer.AddVertex(0);
	outer.AddVertex(1);

	CHECK(outer.Flush(false).vertices.empty());

	outer.AddVertex(2);
	outer.AddVertex(3);
	const auto completed = outer.Flush(true);

	CHECK(completed.vertices == std::vector<int> {0, 1, 2, 0, 2, 3});
	CHECK(completed.discardedVertices == 0);
}

TEST_CASE("LuaOpenGLCoreBatch preserves strip winding across flushes")
{
	PrimitiveAssembler<int> assembler(PrimitiveMode::TriangleStrip);
	assembler.AddVertex(0);
	assembler.AddVertex(1);
	assembler.AddVertex(2);
	const auto prefix = assembler.Flush(false);

	assembler.AddVertex(3);
	assembler.AddVertex(4);
	const auto suffix = assembler.Flush(true);

	CHECK(prefix.vertices == std::vector<int> {0, 1, 2});
	CHECK(suffix.vertices == std::vector<int> {
		2, 1, 3,
		2, 3, 4,
	});
}

TEST_CASE("LuaOpenGLCoreBatch preserves fan connectivity across flushes")
{
	PrimitiveAssembler<int> assembler(PrimitiveMode::TriangleFan);
	assembler.AddVertex(0);
	assembler.AddVertex(1);
	assembler.AddVertex(2);
	const auto prefix = assembler.Flush(false);

	assembler.AddVertex(3);
	assembler.AddVertex(4);
	const auto suffix = assembler.Flush(true);

	CHECK(prefix.vertices == std::vector<int> {0, 1, 2});
	CHECK(suffix.vertices == std::vector<int> {
		0, 2, 3,
		0, 3, 4,
	});
}

TEST_CASE("LuaOpenGLCoreBatch preserves line-loop connectivity across flushes")
{
	PrimitiveAssembler<int> assembler(PrimitiveMode::LineLoop);
	assembler.AddVertex(0);
	assembler.AddVertex(1);
	assembler.AddVertex(2);
	const auto prefix = assembler.Flush(false);

	assembler.AddVertex(3);
	const auto suffix = assembler.Flush(true);

	CHECK(prefix.vertices == std::vector<int> {0, 1, 1, 2});
	CHECK(suffix.vertices == std::vector<int> {2, 3, 3, 0});
}

TEST_CASE("LuaOpenGLCoreBatch preserves quad-strip connectivity across flushes")
{
	PrimitiveAssembler<int> assembler(PrimitiveMode::QuadStrip);
	assembler.AddVertex(0);
	assembler.AddVertex(1);
	assembler.AddVertex(2);
	assembler.AddVertex(3);
	const auto prefix = assembler.Flush(false);

	assembler.AddVertex(4);
	assembler.AddVertex(5);
	const auto suffix = assembler.Flush(true);

	CHECK(prefix.vertices == std::vector<int> {0, 1, 3, 0, 3, 2});
	CHECK(suffix.vertices == std::vector<int> {2, 3, 5, 2, 5, 4});
	CHECK(suffix.discardedVertices == 0);
}

TEST_CASE("LuaOpenGLCoreBatch reports only incomplete final primitives")
{
	PrimitiveAssembler<int> quads(PrimitiveMode::Quads);
	for (int vertex = 0; vertex < 6; ++vertex)
		quads.AddVertex(vertex);

	const auto quadBatch = quads.Flush(true);
	CHECK(quadBatch.vertices == std::vector<int> {0, 1, 2, 0, 2, 3});
	CHECK(quadBatch.discardedVertices == 2);

	PrimitiveAssembler<int> quadStrip(PrimitiveMode::QuadStrip);
	for (int vertex = 0; vertex < 5; ++vertex)
		quadStrip.AddVertex(vertex);

	const auto stripBatch = quadStrip.Flush(true);
	CHECK(stripBatch.vertices == std::vector<int> {0, 1, 3, 0, 3, 2});
	CHECK(stripBatch.discardedVertices == 1);
}

TEST_CASE("LuaOpenGLCoreBatch segmented output matches uninterrupted output")
{
	const std::vector<PrimitiveMode> modes {
		PrimitiveMode::Points,
		PrimitiveMode::Lines,
		PrimitiveMode::LineLoop,
		PrimitiveMode::LineStrip,
		PrimitiveMode::Triangles,
		PrimitiveMode::TriangleStrip,
		PrimitiveMode::TriangleFan,
		PrimitiveMode::Quads,
		PrimitiveMode::QuadStrip,
		PrimitiveMode::Polygon,
	};

	for (const PrimitiveMode mode: modes) {
		for (int vertexCount = 0; vertexCount <= 12; ++vertexCount) {
			PrimitiveAssembler<int> uninterrupted(mode);
			PrimitiveAssembler<int> segmented(mode);
			std::vector<int> segmentedVertices;

			for (int vertex = 0; vertex < vertexCount; ++vertex) {
				uninterrupted.AddVertex(vertex);
				segmented.AddVertex(vertex);

				const auto segment = segmented.Flush(false);
				segmentedVertices.insert(
					segmentedVertices.end(),
					segment.vertices.begin(),
					segment.vertices.end()
				);
			}

			const auto expected = uninterrupted.Flush(true);
			const auto finalSegment = segmented.Flush(true);
			segmentedVertices.insert(
				segmentedVertices.end(),
				finalSegment.vertices.begin(),
				finalSegment.vertices.end()
			);

			CAPTURE(mode, vertexCount);
			CHECK(segmentedVertices == expected.vertices);
			CHECK(finalSegment.discardedVertices == expected.discardedVertices);
		}
	}
}
