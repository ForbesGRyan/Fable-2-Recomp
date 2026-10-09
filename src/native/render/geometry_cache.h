#pragma once

// Decoded guest geometry on the GPU for the clay pass: float4 positions,
// float2 UVs and uint32 triangle-list indices in persistently mapped upload-heap buffers,
// read by the vertex shader as StructuredBuffers (the RHI has 16-bit index
// buffers only and no buffer-to-buffer copy). For the light also float3
// normals and uint32 vertex colours (normal_decode.h), the probe weights with
// their set indices, and for the instanced plants the copies' colours and the
// bend inputs.
// Keyed by GeometryCacheIndex (guest range + layout + content hash) with
// an LRU byte budget that counts every buffer.
// GPU (command-processor) thread only.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <rex/graphics/native_rhi.h>

#include "../capture/draw_record.h"
#include "clay_logic.h"
#include "geometry_cache_index.h"

namespace fable2::native::render {

namespace nrhi = rex::graphics::nrhi;

class GeometryCache {
 public:
  explicit GeometryCache(uint64_t budget_bytes);

  // Returns the GPU buffer holding float4 positions / uint32 indices, or
  // nullptr (draw skipped). *vertex_count receives the number of positions
  // the returned buffer actually holds. An instanced record's buffer is its
  // flat stream (instance_expand.h): instances.flat_count positions, indexed
  // like any other draw. `terrain`: the record's terrain inputs from its
  // scene's side array (TerrainOf), null for a record that is not a terrain
  // record; a terrain record's buffers are the grid built from them.
  // `bounds` (optional): receives the box of the buffer's positions (PositionBounds, clay_logic.h),
  // computed once when the buffer is built; not valid when no buffer is returned.
  nrhi::Buffer* Positions(nrhi::Device* dev, const capture::DrawRecord& r, const capture::TerrainPatch* terrain,
                          uint32_t* vertex_count, ClayStats& st, PositionBounds* bounds = nullptr);
  // `max_index` (optional): receives the largest index of the list, before the base vertex.
  nrhi::Buffer* Indices(nrhi::Device* dev, const capture::DrawRecord& r, const capture::TerrainPatch* terrain,
                        uint32_t vertex_count, uint32_t* index_count, ClayStats& st, uint32_t* max_index = nullptr);
  // float2 UVs of the record's material (material.uv_vb / material.uv), one
  // per vertex of the UV stream; nullptr when the stream is unreadable, does
  // not decode or holds fewer than `vertex_count` UVs (the shader reads
  // uvs[v] for every v < vertex_count). Reads are bounded by uv_vb.size. An
  // instanced record gets flat UVs: the mesh vertex's UV per flat position.
  nrhi::Buffer* Uvs(nrhi::Device* dev, const capture::DrawRecord& r, uint32_t vertex_count,
                    ClayStats& st);
  // float2 far colour coordinates of a terrain record of the adaptive builder
  // (`t`: its terrain inputs, here and in TerrainNormals),
  // one per grid point of its position buffer: read from the vertex shader's
  // two tables where the game reads them, at the patch knots
  // (capture::BuildTerrainFarUvs; key TerrainFarUvKey, content hash of both
  // tables). nullptr when the record's material inputs are not valid ones of
  // that builder, a table is not a k_16 texture the heightmap decode handles or
  // is unreadable, a knot lies outside its table's bytes, or fewer than
  // `vertex_count` coordinates result. Reads are bounded by the tables' sizes.
  nrhi::Buffer* TerrainFarUvs(nrhi::Device* dev, const capture::TerrainPatch& t, uint32_t vertex_count,
                              ClayStats& st);
  // float2 (n.x, n.y) of the game's vertex normal for a terrain record (a layer
  // with or without the steep-ground block, a draw of the adaptive builder),
  // one per grid point of its position buffer: read from the terrain vertex
  // shader's normal map where the game reads it, at the grid points
  // (capture::BuildTerrainNormals; key TerrainNormalKey, content hash of the
  // map). The clay vertex shader forms z. nullptr when the record's material
  // inputs are not valid, they hold no normal map (the slot held no texture),
  // the map is not a k_DXN texture DecodeNormalMap handles or is unreadable, a
  // grid point's texel lies outside the map's bytes, or fewer than
  // `vertex_count` normals result. Reads are bounded by the map's size.
  nrhi::Buffer* TerrainNormals(nrhi::Device* dev, const capture::TerrainPatch& t, uint32_t vertex_count,
                               ClayStats& st);
  // float3 unit normals of a mesh record with light inputs (`light`: LightOf),
  // one per vertex of its normal stream (at least `need`; an instanced draw:
  // one per flat position) and in the space of its positions
  // (normal_decode.h): a plain mesh's as the stream stores them (the draw's
  // world rotation, light.world_rot, is not applied: one buffer for every draw
  // of the mesh; key MeshNormalKey), a skinned mesh's after the bones, an
  // instanced draw's after the instance's rows, one per flat position (keys
  // NormalKey). A vertex without a direction holds (0, 0, 0). nullptr, with
  // the reason in `why`: normal-unsupported for a normal element no reader
  // decodes (NormalElementSupported); light-unknown when a stream is
  // unreadable, the buffer would hold fewer than `need` normals, a bone word
  // lies outside its stream, or the upload fails. `need`: the vertices the
  // draw's indices name (LightVertexNeed: its largest vertex plus one, the
  // capture's rule for the stream; for an instanced draw its flat positions).
  // `count` receives the normals the buffer holds: the shader reads
  // normals[v] only for v below it (ClayConstants::light_count). Reads are
  // bounded by the streams' sizes. A terrain record has TerrainNormals instead.
  nrhi::Buffer* Normals(nrhi::Device* dev, const capture::DrawRecord& r, const capture::LightInputs& light,
                        uint32_t need, ClayStats& st, capture::LightReason* why, uint32_t* count);
  // uint32 colours of a record whose family has a colour stream
  // (LightVertexKind::kStream): element light.colour_first + v of the stream
  // for vertex v, for as many vertices as the stream holds from that offset
  // on and the record has positions, red, green, blue and
  // alpha in bytes 0 to 3 (DecodeColours; key ColourKey, content hash of the
  // stream). nullptr when the element is not a colour a reader decodes, the
  // stream is unreadable, or it holds fewer than `need` colours from
  // colour_first on (`need` and `count` as Normals'): nothing is read past
  // the stream's size, and a draw whose offset leads outside it has no colours.
  nrhi::Buffer* Colours(nrhi::Device* dev, const capture::DrawRecord& r, const capture::LightInputs& light,
                        uint32_t need, ClayStats& st, uint32_t* count);
  // The probe buffer of a record whose vertex kind is probe
  // (LightVertexKind::kProbe), one entry per vertex its weights stream and its
  // position stream both hold: the four weights of the vertex (float4, from
  // byte 0) and its set index a0 (one uint32: 0, 4 or 8; from
  // ProbeSetOffset(count) on), as BuildProbe lays them out (normal_decode.h;
  // key ProbeKey, content hash of the two streams). Mesh-intrinsic: the probe
  // constants c28..c39 are the draw's (light.vs_const) and are not in it, and
  // a skinned mesh's entry does not depend on its palette. All or nothing:
  // nullptr, with the cause in `why`, when the weights' selector or the set's
  // component is not the one its format has (element), a stream is unreadable
  // or holds fewer than `need` vertices (stream), a vertex's set index is not
  // 0, 4 or 8 (probe-set), a weight is not a number of usable size (value), or
  // the upload fails. `need` and `count` as Normals'. Reads are bounded by the
  // streams' sizes. The clay vertex shader reads it at t10 (the sets) and t11
  // (the weights) for a lit probe record.
  nrhi::Buffer* Probe(nrhi::Device* dev, const capture::DrawRecord& r, const capture::LightInputs& light, uint32_t need,
                      ClayStats& st, LightSecond* why, uint32_t* count);
  // uint32 colours of an instanced record (LightVertexKind::kInstance), one per
  // flat position: the colour of the position's copy, the 8_8_8_8 element of
  // the instance stream, red, green and blue in bytes 0 to 2 as stored, byte 3
  // zero (ExpandInstanceColours; key InstanceColourKey: the element and the
  // instance set as the flat normals have it; content hash of the element's
  // stream). nullptr, with the cause in `why`, when the element's selector is
  // not an instance colour's (element), the stream is unreadable, the record
  // has fewer flat positions than `need`, or a copy the draw names has its
  // colour past the stream (stream), or the upload fails. Reads are bounded by
  // the stream's size. The clay vertex shader reads it at t10 for a lit plant.
  nrhi::Buffer* InstanceColours(nrhi::Device* dev, const capture::DrawRecord& r, const capture::LightInputs& light,
                                uint32_t need, ClayStats& st, LightSecond* why, uint32_t* count);
  // float4 bend inputs of an instanced record (the plants' bent normal), one
  // per flat position: the up axis of the position's copy (xyz: the third
  // column of the copy's rows, normalised; zero for a copy without one) and
  // the height of its mesh vertex in the copy's space (w: the decoded
  // position's third component), as ExpandInstanceBend makes them
  // (normal_decode.h; key InstanceBendKey: the mesh's position stream and
  // layout, and the instance set as the flat normals have it; content hash of
  // the mesh stream and the instance stream). The draw's bend constants c45
  // are not in it. nullptr, with the cause in `why`, when a stream is
  // unreadable or the record has fewer flat positions than `need` (stream), a
  // vertex the draw names has a height that is not a number (value: all or
  // nothing), or the upload fails. `need` and `count` as Normals'; `bytes`
  // receives the buffer's allocation (for the log's bend line). Reads are
  // bounded by the streams' sizes. The clay vertex shader reads it at t11 for
  // a lit plant.
  nrhi::Buffer* InstanceBend(nrhi::Device* dev, const capture::DrawRecord& r, uint32_t need, ClayStats& st,
                             LightSecond* why, uint32_t* count, uint64_t* bytes);

  void BeginFrame(uint64_t frame, uint64_t budget_bytes);
  // DestroyDeferred all (dev may be nullptr: forget the buffers without
  // destroying them, e.g. after a device change).
  void Release(nrhi::Device* dev);

  uint64_t resident_bytes() const { return index_.resident_bytes(); }

 private:
  struct Entry {
    nrhi::Buffer* buffer = nullptr;
    uint32_t count = 0;      // vertices or indices
    uint32_t max_index = 0;  // indices only
    uint64_t bytes = 0;      // allocation size
    uint32_t min_index = 0;  // indices only: the smallest index of the list
    PositionBounds bounds;   // positions only: the box of the buffer's vertices
  };
  // Replaced buffers, reused by Upload (animated skinned streams re-decode
  // every frame; creating a buffer costs far more than filling one).
  static constexpr uint64_t kPoolBudgetBytes = 32ull << 20;

  nrhi::Buffer* Upload(nrhi::Device* dev, const void* data, uint64_t bytes, uint64_t* alloc_bytes);
  // Content hash of a guest range, computed once per frame.
  uint64_t FrameHash(uint32_t phys_addr, uint32_t size, const uint8_t* data);
  nrhi::Buffer* TerrainPositions(nrhi::Device* dev, const capture::DrawRecord& r, const capture::TerrainPatch& t,
                                 uint32_t* vertex_count, ClayStats& st, PositionBounds* bounds);
  // Instanced records: the flat position stream built from the mesh stream
  // and the instance stream, rebuilt when either's bytes or the instance set
  // (the key) change; the flat UV stream built from the mesh's UVs alone,
  // rebuilt when their bytes or the index mapping (UvKey) change.
  nrhi::Buffer* InstancedPositions(nrhi::Device* dev, const capture::DrawRecord& r,
                                   uint32_t* vertex_count, ClayStats& st, PositionBounds* bounds);
  nrhi::Buffer* InstancedUvs(nrhi::Device* dev, const capture::DrawRecord& r,
                             uint32_t vertex_count, ClayStats& st);
  // The flat normal stream of an instanced record, built from the mesh's
  // normals and the instance stream's rows; rebuilt when either's bytes or the
  // instance set but for its offset (NormalKey) change.
  nrhi::Buffer* InstancedNormals(nrhi::Device* dev, const capture::DrawRecord& r, const capture::LightInputs& light,
                                 uint32_t vertex_count, ClayStats& st);
  // The first `count` positions / UVs of the record's mesh stream, decoded
  // (`mesh` and `mesh_hash` are the stream's bytes and frame hash), or
  // nullptr if they do not decode. Kept for the frame per stream and layout,
  // so the instanced draws that expand one mesh decode it once (a draw with
  // another count of the same stream decodes again); valid until the next
  // call.
  const capture::Float4* MeshPositions(const capture::DrawRecord& r, const uint8_t* mesh,
                                       uint64_t mesh_hash, uint32_t count);
  const capture::Float2* MeshUvs(const capture::DrawRecord& r, const uint8_t* mesh,
                                 uint64_t mesh_hash, uint32_t count);
  uint32_t Insert(nrhi::Device* dev, const GeoKey& key, uint64_t hash, const Entry& e,
                  uint64_t bytes);

  void Retire(nrhi::Device* dev, nrhi::Buffer* buffer, uint64_t bytes);

  GeometryCacheIndex index_;
  RetirePool<nrhi::Buffer*> pool_{kPoolBudgetBytes};
  std::vector<nrhi::Buffer*> destroy_;
  std::unordered_map<uint32_t, Entry> entries_;
  // Content hash per guest range, computed once per frame (many draws share
  // one vertex stream).
  std::unordered_map<uint64_t, uint64_t> frame_hashes_;
  std::vector<capture::Float4> positions_;
  std::vector<capture::Float2> uvs_;
  // Decoded mesh streams of this frame's instanced draws (cleared in
  // BeginFrame): every draw of a mesh has its own flat positions (key kind
  // 5), but the mesh is decoded once. 64 MB together; a mesh that does
  // not fit is decoded into the scratch vectors below for its draw alone.
  static constexpr uint64_t kMeshPositionMemoBytes = 48ull << 20;
  static constexpr uint64_t kMeshUvMemoBytes = 16ull << 20;
  FrameMemo<capture::Float4> mesh_position_memo_{kMeshPositionMemoBytes};
  FrameMemo<capture::Float2> mesh_uv_memo_{kMeshUvMemoBytes};
  std::vector<capture::Float4> mesh_positions_;
  std::vector<capture::Float2> mesh_uvs_;
  std::vector<Float3> normals_;       // a record's normals being built
  std::vector<Float3> mesh_normals_;  // an instanced record's mesh normals (one copy's vertices)
  std::vector<uint32_t> colours_;     // a record's colours (a colour stream's, an instance set's) being built
  std::vector<uint8_t> probe_;        // a mesh's probe buffer being built (BuildProbe)
  std::vector<capture::Float4> bend_; // an instanced record's bend inputs being built (ExpandInstanceBend)
  std::vector<uint32_t> indices_;
  std::vector<uint32_t> evicted_;
};

}  // namespace fable2::native::render
