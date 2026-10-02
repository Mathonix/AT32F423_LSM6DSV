// Offline STEP -> glTF 2.0. The browser never downloads the CAD parser or STEP.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { basename } from 'node:path';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const input = process.argv[2];
if (!input) throw new Error('Usage: node scripts/convert-pcb.mjs model.step [mesh-cache.json]');
const source = readFileSync(input);
const params = { linearUnit: 'millimeter', linearDeflectionType: 'absolute_value', linearDeflection: .08, angularDeflection: .35 };
const result = process.argv[3] ? JSON.parse(readFileSync(process.argv[3], 'utf8'))
  : (await require('occt-import-js')()).ReadStepFile(source, params);
if (!result.success || !result.meshes.length) throw new Error('STEP contains no imported meshes');
const board = result.meshes.find(m => m.name?.startsWith('Board'));
if (!board) throw new Error('PCB board reference not found');
function bounds(mesh) {
  const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  mesh.attributes.position.array.forEach((x, i) => { if (!Number.isFinite(x)) throw new Error('Invalid vertex'); min[i % 3] = Math.min(min[i % 3], x); max[i % 3] = Math.max(max[i % 3], x); });
  return { min, max };
}
const boardBounds = bounds(board);
// Center on the PCB rather than the asymmetric connectors. Keep STEP X/Y/Z.
const center = boardBounds.min.map((v, i) => (v + boardBounds.max[i]) / 2);
const scale = 2.6 / Math.max(boardBounds.max[0] - boardBounds.min[0], boardBounds.max[1] - boardBounds.min[1]);
const vertices = result.meshes.reduce((n, m) => n + m.attributes.position.array.length / 3, 0);
const positions = new Float32Array(vertices * 3), normals = new Float32Array(vertices * 3);
const colorGroups = new Map();
let offset = 0, triangles = 0;
for (const mesh of result.meshes) {
  const p = mesh.attributes.position.array, n = mesh.attributes.normal?.array;
  if (!n || n.length !== p.length) throw new Error('STEP normals missing');
  for (let i = 0; i < p.length; i++) { positions[offset * 3 + i] = (p[i] - center[i % 3]) * scale; normals[offset * 3 + i] = n[i]; }
  const colors = new Array(mesh.index.array.length / 3).fill(mesh.color || [.6, .6, .6]);
  for (const face of mesh.brep_faces) if (face.color) for (let t = face.first; t <= face.last; t++) colors[t] = face.color;
  for (let t = 0; t < colors.length; t++) {
    const rgb = colors[t], key = rgb.map(x => x.toFixed(6)).join(',');
    if (!colorGroups.has(key)) colorGroups.set(key, { rgb, indices: [] });
    const group = colorGroups.get(key);
    for (let j = 0; j < 3; j++) {
      const index = mesh.index.array[t * 3 + j];
      if (index < 0 || index >= p.length / 3) throw new Error('Invalid mesh index');
      group.indices.push(index + offset);
    }
  }
  offset += p.length / 3; triangles += mesh.index.array.length / 3;
}
const gltf = { asset: { version: '2.0', generator: 'gyro-live / occt-import-js 0.0.23' }, scene: 0,
  scenes: [{ nodes: [0] }], nodes: [{ name: 'AT32 PCB', mesh: 0 }], meshes: [{ name: basename(input), primitives: [] }],
  materials: [], accessors: [], bufferViews: [], buffers: [] };
const chunks = []; let byteLength = 0;
function accessor(array, componentType, type, target, extra = {}) {
  const bytes = Buffer.from(array.buffer, array.byteOffset, array.byteLength);
  const view = gltf.bufferViews.push({ buffer: 0, byteOffset: byteLength, byteLength: bytes.length, target }) - 1;
  chunks.push(bytes); byteLength += bytes.length;
  return gltf.accessors.push({ bufferView: view, componentType, count: array.length / (type === 'VEC3' ? 3 : 1), type, ...extra }) - 1;
}
const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
positions.forEach((v, i) => { min[i % 3] = Math.min(min[i % 3], v); max[i % 3] = Math.max(max[i % 3], v); });
const position = accessor(positions, 5126, 'VEC3', 34962, { min, max });
const normal = accessor(normals, 5126, 'VEC3', 34962);
const linear = c => c <= .04045 ? c / 12.92 : ((c + .055) / 1.055) ** 2.4;
for (const { rgb, indices } of colorGroups.values()) {
  const metallic = Math.max(...rgb) - Math.min(...rgb) < .08 && rgb[0] > .4 && rgb[0] < .9;
  const material = gltf.materials.push({ name: `STEP RGB ${rgb.map(x => x.toFixed(3)).join(' ')}`,
    pbrMetallicRoughness: { baseColorFactor: [...rgb.map(linear), 1], metallicFactor: metallic ? .45 : .05, roughnessFactor: metallic ? .38 : .65 }, doubleSided: true }) - 1;
  gltf.meshes[0].primitives.push({ attributes: { POSITION: position, NORMAL: normal }, indices: accessor(new Uint32Array(indices), 5125, 'SCALAR', 34963), material });
}
gltf.buffers.push({ byteLength });
const provenance = { source: basename(input), sourceSha256: createHash('sha256').update(source).digest('hex'), sourceBytes: source.length,
  converter: 'occt-import-js 0.0.23', tessellation: params, componentMeshes: result.meshes.length, triangles, vertices,
  materialCount: colorGroups.size, boardDimensionsMm: boardBounds.max.map((v, i) => v - boardBounds.min[i]),
  bodyAxes: 'STEP X / Y / Z preserved; PCB center is rotation origin', centerMm: center, displayScale: scale,
  bounds: { min, max }, componentNames: [...new Set(result.meshes.map(m => m.name).filter(Boolean))] };
gltf.extras = provenance;
let json = Buffer.from(JSON.stringify(gltf)); json = Buffer.concat([json, Buffer.alloc((4 - json.length % 4) % 4, 32)]);
const binary = Buffer.concat(chunks);
const header = Buffer.alloc(12); header.writeUInt32LE(0x46546c67); header.writeUInt32LE(2, 4); header.writeUInt32LE(12 + 8 + json.length + 8 + binary.length, 8);
const jsonHeader = Buffer.alloc(8); jsonHeader.writeUInt32LE(json.length); jsonHeader.writeUInt32LE(0x4e4f534a, 4);
const binHeader = Buffer.alloc(8); binHeader.writeUInt32LE(binary.length); binHeader.writeUInt32LE(0x004e4942, 4);
const glb = Buffer.concat([header, jsonHeader, json, binHeader, binary]);
const out = new URL('../src/public/models/', import.meta.url); mkdirSync(out, { recursive: true });
writeFileSync(new URL('pcb.glb', out), glb);
writeFileSync(new URL('pcb-source.json', out), JSON.stringify({ ...provenance, glbBytes: glb.length, glbSha256: createHash('sha256').update(glb).digest('hex') }, null, 2));
console.log(JSON.stringify({ meshes: result.meshes.length, triangles, materials: colorGroups.size, glbBytes: glb.length, boardDimensionsMm: provenance.boardDimensionsMm }));
