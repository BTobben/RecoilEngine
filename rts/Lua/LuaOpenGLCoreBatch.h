/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace LuaOpenGLCore {

enum class PrimitiveMode {
	Points,
	Lines,
	LineLoop,
	LineStrip,
	Triangles,
	TriangleStrip,
	TriangleFan,
	Quads,
	QuadStrip,
	Polygon,
};

template<typename VertexType>
struct PrimitiveBatch {
	PrimitiveMode mode = PrimitiveMode::Points;
	std::vector<VertexType> vertices;
	std::size_t discardedVertices = 0;
};

/**
 * Converts legacy immediate-mode primitives to Core-profile primitives.
 *
 * Flush(false) emits every complete primitive while retaining the minimum
 * amount of connectivity data needed to resume the source primitive later.
 * This permits a caller to suspend an outer gl.BeginEnd callback, draw a
 * re-entrant inner callback in the correct order, and then resume the outer
 * primitive without relying on compatibility-profile OpenGL state.
 */
template<typename VertexType>
class PrimitiveAssembler
{
public:
	explicit PrimitiveAssembler(const PrimitiveMode mode)
		: sourceMode(mode)
	{}

	void Reset(const PrimitiveMode mode)
	{
		sourceMode = mode;
		pendingVertices.clear();
		firstVertex.reset();
		lastVertex.reset();
		vertexCount = 0;
		triangleCount = 0;
	}

	void AddVertex(const VertexType& vertex)
	{
		if (!firstVertex.has_value())
			firstVertex = vertex;

		pendingVertices.push_back(vertex);
		++vertexCount;
	}

	void AddVertex(VertexType&& vertex)
	{
		if (!firstVertex.has_value())
			firstVertex = vertex;

		pendingVertices.push_back(std::move(vertex));
		++vertexCount;
	}

	[[nodiscard]] PrimitiveBatch<VertexType> Flush(const bool finish)
	{
		PrimitiveBatch<VertexType> result;
		Flush(finish, result);
		return result;
	}

	void Flush(const bool finish, PrimitiveBatch<VertexType>& result)
	{
		result.vertices.clear();
		result.discardedVertices = 0;

		switch (sourceMode) {
			case PrimitiveMode::Points:
				result.mode = PrimitiveMode::Points;
				MovePrefix(result.vertices, pendingVertices.size());
				break;

			case PrimitiveMode::Lines:
				result.mode = PrimitiveMode::Lines;
				FlushIndependent(result, 2, finish);
				break;

			case PrimitiveMode::LineLoop:
				result.mode = PrimitiveMode::Lines;
				FlushLineStrip(result);
				if (finish && vertexCount >= 2 && firstVertex.has_value() && lastVertex.has_value()) {
					result.vertices.push_back(*lastVertex);
					result.vertices.push_back(*firstVertex);
				}
				if (finish && vertexCount < 2)
					result.discardedVertices = vertexCount;
				break;

			case PrimitiveMode::LineStrip:
				result.mode = PrimitiveMode::Lines;
				FlushLineStrip(result);
				if (finish && vertexCount < 2)
					result.discardedVertices = vertexCount;
				break;

			case PrimitiveMode::Triangles:
				result.mode = PrimitiveMode::Triangles;
				FlushIndependent(result, 3, finish);
				break;

			case PrimitiveMode::TriangleStrip:
				result.mode = PrimitiveMode::Triangles;
				FlushTriangleStrip(result);
				if (finish && vertexCount < 3)
					result.discardedVertices = vertexCount;
				break;

			case PrimitiveMode::TriangleFan:
			case PrimitiveMode::Polygon:
				result.mode = PrimitiveMode::Triangles;
				FlushTriangleFan(result);
				if (finish && vertexCount < 3)
					result.discardedVertices = vertexCount;
				break;

			case PrimitiveMode::Quads:
				result.mode = PrimitiveMode::Triangles;
				FlushQuads(result, finish);
				break;

			case PrimitiveMode::QuadStrip:
				result.mode = PrimitiveMode::Triangles;
				FlushQuadStrip(result, finish);
				break;
		}

		if (finish)
			pendingVertices.clear();
	}

private:
	void MovePrefix(std::vector<VertexType>& destination, const std::size_t count)
	{
		destination.reserve(destination.size() + count);
		for (std::size_t index = 0; index < count; ++index)
			destination.push_back(std::move(pendingVertices[index]));

		pendingVertices.erase(pendingVertices.begin(), pendingVertices.begin() + count);
	}

	void FlushIndependent(
		PrimitiveBatch<VertexType>& result,
		const std::size_t verticesPerPrimitive,
		const bool finish
	) {
		const std::size_t completeVertexCount =
			pendingVertices.size() - (pendingVertices.size() % verticesPerPrimitive);

		MovePrefix(result.vertices, completeVertexCount);

		if (finish) {
			result.discardedVertices = pendingVertices.size();
			pendingVertices.clear();
		}
	}

	void FlushLineStrip(PrimitiveBatch<VertexType>& result)
	{
		if (pendingVertices.empty())
			return;

		lastVertex = pendingVertices.back();

		if (pendingVertices.size() >= 2) {
			result.vertices.reserve((pendingVertices.size() - 1) * 2);
			for (std::size_t index = 1; index < pendingVertices.size(); ++index) {
				result.vertices.push_back(pendingVertices[index - 1]);
				result.vertices.push_back(pendingVertices[index]);
			}
		}

		const VertexType tail = std::move(pendingVertices.back());
		pendingVertices.clear();
		pendingVertices.push_back(std::move(tail));
	}

	void FlushTriangleStrip(PrimitiveBatch<VertexType>& result)
	{
		if (pendingVertices.size() >= 3) {
			result.vertices.reserve((pendingVertices.size() - 2) * 3);
			for (std::size_t index = 2; index < pendingVertices.size(); ++index) {
				if ((triangleCount % 2) == 0) {
					result.vertices.push_back(pendingVertices[index - 2]);
					result.vertices.push_back(pendingVertices[index - 1]);
				} else {
					result.vertices.push_back(pendingVertices[index - 1]);
					result.vertices.push_back(pendingVertices[index - 2]);
				}
				result.vertices.push_back(pendingVertices[index]);
				++triangleCount;
			}
		}

		if (pendingVertices.size() > 2)
			pendingVertices.erase(pendingVertices.begin(), pendingVertices.end() - 2);
	}

	void FlushTriangleFan(PrimitiveBatch<VertexType>& result)
	{
		if (!firstVertex.has_value())
			return;

		const std::size_t firstOuterIndex = (triangleCount == 0) ? 2 : 1;
		if (pendingVertices.size() > firstOuterIndex) {
			result.vertices.reserve((pendingVertices.size() - firstOuterIndex) * 3);
			for (std::size_t index = firstOuterIndex; index < pendingVertices.size(); ++index) {
				result.vertices.push_back(*firstVertex);
				result.vertices.push_back(pendingVertices[index - 1]);
				result.vertices.push_back(pendingVertices[index]);
				++triangleCount;
			}
		}

		if (triangleCount > 0 && !pendingVertices.empty()) {
			const VertexType tail = std::move(pendingVertices.back());
			pendingVertices.clear();
			pendingVertices.push_back(std::move(tail));
		}
	}

	static void AddQuadTriangles(
		std::vector<VertexType>& destination,
		const VertexType& vertex0,
		const VertexType& vertex1,
		const VertexType& vertex2,
		const VertexType& vertex3
	) {
		destination.push_back(vertex0);
		destination.push_back(vertex1);
		destination.push_back(vertex2);
		destination.push_back(vertex0);
		destination.push_back(vertex2);
		destination.push_back(vertex3);
	}

	void FlushQuads(PrimitiveBatch<VertexType>& result, const bool finish)
	{
		const std::size_t completeVertexCount = pendingVertices.size() - (pendingVertices.size() % 4);
		result.vertices.reserve((completeVertexCount / 4) * 6);

		for (std::size_t index = 0; index < completeVertexCount; index += 4) {
			AddQuadTriangles(
				result.vertices,
				pendingVertices[index + 0],
				pendingVertices[index + 1],
				pendingVertices[index + 2],
				pendingVertices[index + 3]
			);
		}

		pendingVertices.erase(pendingVertices.begin(), pendingVertices.begin() + completeVertexCount);
		if (finish) {
			result.discardedVertices = pendingVertices.size();
			pendingVertices.clear();
		}
	}

	void FlushQuadStrip(PrimitiveBatch<VertexType>& result, const bool finish)
	{
		std::size_t firstRetainedIndex = 0;
		result.vertices.reserve((pendingVertices.size() / 2) * 6);

		for (
			std::size_t index = 0;
			index + 3 < pendingVertices.size();
			index += 2
		) {
			AddQuadTriangles(
				result.vertices,
				pendingVertices[index + 0],
				pendingVertices[index + 1],
				pendingVertices[index + 3],
				pendingVertices[index + 2]
			);
			firstRetainedIndex = index + 2;
		}

		if (firstRetainedIndex > 0)
			pendingVertices.erase(pendingVertices.begin(), pendingVertices.begin() + firstRetainedIndex);

		if (finish) {
			result.discardedVertices = vertexCount % 2;
			pendingVertices.clear();
		}
	}

private:
	PrimitiveMode sourceMode;
	std::vector<VertexType> pendingVertices;
	std::optional<VertexType> firstVertex;
	std::optional<VertexType> lastVertex;
	std::size_t vertexCount = 0;
	std::size_t triangleCount = 0;
};

} // namespace LuaOpenGLCore
