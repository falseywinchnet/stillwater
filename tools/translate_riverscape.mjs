// Offline geometry translation only. No JavaScript or Three.js ships in the app.
// Upstream source is preserved verbatim in upstream/ with its MIT license.
import * as THREE from './upstream/vendor/three.module.js';
import { createEnvironment } from './upstream/scenes/riverscape/src/environment.js';
import { createPlants } from './upstream/scenes/riverscape/src/plants.js';
import { makeAnatomy } from './upstream/scenes/riverscape/src/fish-anatomy.js';
import { writeFileSync } from 'node:fs';
import { gzipSync } from 'node:zlib';

// Image pixels are loaded by ImageIO in the native app. Here we need only mapping metadata.
async function loadTexture(path) {
  const texture = new THREE.Texture();
  texture.name = path;
  return texture;
}
THREE.TextureLoader.prototype.loadAsync = loadTexture;
// Upstream's contact-shadow planes are replaced by native depth shadows. Consume the
// same construction path without creating a browser or changing the random sequence.
function ignore() {}
function gradient() { return { addColorStop: ignore }; }
function context() { return { createRadialGradient: gradient, fillRect: ignore }; }
function canvas() { return { getContext: context }; }
globalThis.document = { createElement: canvas };

const scene = new THREE.Scene();
await createEnvironment(scene);
const planting = createPlants(scene);
scene.updateMatrixWorld(true);
const vertices = [], indices = [], instances = [], objects = [], batches = [], actors = [];
const identity = new THREE.Matrix4();
const temporaryMatrix = new THREE.Matrix4();
const temporaryColor = new THREE.Color();
function attribute(geometry, name, index, count, fallback) {
  const value = geometry.getAttribute(name);
  if (!value) return fallback;
  const result = [];
  for (let component = 0; component < count; ++component)
    result.push(value.array[index * value.itemSize + component]);
  return result;
}
function addInstance(matrix, color, material, repeat, kind, center, radius, actor = -1) {
  const index = instances.length;
  instances.push([...matrix.elements, color.r, color.g, color.b, 1,
    material, repeat[0], repeat[1], actor, 0, 0, 0, 0]);
  objects.push({ kind, instance: index, center: [center.x, center.y, center.z], radius, actor });
  return index;
}
function materialKind(mesh) {
  const name = mesh.material.map?.name ?? '';
  if (name.includes('sand_01')) return [7, 0];
  if (name.includes('rock_boulder')) return [8, 1];
  if (name.includes('rough_wood')) return [9, 7];
  if (mesh.geometry.hasAttribute('anchor')) return [10, 2];
  if (mesh.material.color.getHex() === 0xb6a07a) return [14, 1];
  return [13, 3];
}
function exportMesh(mesh, materialOverride = null, actorStart = -1) {
  const geometry = mesh.geometry;
  const positions = geometry.getAttribute('position');
  geometry.computeBoundingSphere();
  const pair = materialOverride ?? materialKind(mesh);
  const material = pair[0], kind = pair[1];
  const vertexStart = vertices.length / 32;
  const indexStart = indices.length;
  const instanceStart = instances.length;
  const repeat = mesh.material.map ? [mesh.material.map.repeat.x, mesh.material.map.repeat.y] : [1, 1];
  const leafGroups = new Map();
  const leafBindings = new Uint32Array(positions.count);
  const foliage = material === 10;
  if (foliage) {
    // Retain each attachment root as a separately queryable/movable plant object.
    // The original batches are drawing organization, never scene identity.
    for (let index = 0; index < positions.count; ++index) {
      const root = attribute(geometry, 'anchor', index, 3, [0, 0, 0]);
      const key = root.join(',');
      let group = leafGroups.get(key);
      if (!group) {
        const instance = addInstance(identity, mesh.material.color, material, repeat, kind,
          new THREE.Vector3(...root), 0.01);
        group = { instance, root: new THREE.Vector3(...root), radius: 0 };
        leafGroups.set(key, group);
      }
      const point = new THREE.Vector3().fromBufferAttribute(positions, index);
      group.radius = Math.max(group.radius, point.distanceTo(group.root));
      leafBindings[index] = group.instance;
    }
    for (const group of leafGroups.values()) objects[group.instance].radius = group.radius + 1;
  } else {
    const count = mesh.isInstancedMesh ? mesh.count : 1;
    for (let index = 0; index < count; ++index) {
      temporaryMatrix.copy(mesh.matrixWorld);
      temporaryColor.copy(mesh.material.color);
      if (mesh.isInstancedMesh) {
        const local = new THREE.Matrix4();
        mesh.getMatrixAt(index, local);
        temporaryMatrix.multiply(local);
        if (mesh.instanceColor) {
          const tint = new THREE.Color();
          mesh.getColorAt(index, tint);
          temporaryColor.multiply(tint);
        }
      }
      const sphere = geometry.boundingSphere.clone().applyMatrix4(temporaryMatrix);
      addInstance(temporaryMatrix, temporaryColor, material, repeat, kind, sphere.center,
        sphere.radius, actorStart < 0 ? -1 : actorStart + index);
    }
  }
  for (let index = 0; index < positions.count; ++index) {
    const p = attribute(geometry, 'position', index, 3, [0, 0, 0]);
    const n = attribute(geometry, 'normal', index, 3, [0, 1, 0]);
    const c = attribute(geometry, 'color', index, 3, [1, 1, 1]);
    const uv = attribute(geometry, 'uv', index, 2, [0, 0]);
    const moss = attribute(geometry, 'moss', index, 1, [0])[0];
    const anchor = attribute(geometry, 'anchor', index, 3, [0, 0, 0]);
    const thin = attribute(geometry, 'thin', index, 1, [0])[0];
    const bend = attribute(geometry, 'bend', index, 4, [0, 0, 0, 0]);
    const along = attribute(geometry, 'along', index, 4, [0, 0, 0, 0]);
    const part = attribute(geometry, 'aPart', index, 1, [0])[0];
    const progress = attribute(geometry, 'aFinProgress', index, 1, [0])[0];
    vertices.push(...p, 1, ...n, moss, ...c, 1, ...uv, part, progress,
      ...anchor, thin, ...bend, ...along, leafBindings[index], foliage ? 1 : 0, 1, 0);
  }
  const sourceIndices = geometry.getIndex();
  const count = sourceIndices ? sourceIndices.count : positions.count;
  for (let index = 0; index < count; ++index)
    indices.push(vertexStart + (sourceIndices ? sourceIndices.getX(index) : index));
  batches.push([indexStart, count, instanceStart, foliage ? 1 : instances.length - instanceStart]);
}
for (const mesh of scene.children) {
  if (!mesh.isMesh || mesh.material.isMeshBasicMaterial) continue;
  exportMesh(mesh);
}
const anatomy = makeAnatomy();
const fishCount = 16;
for (let index = 0; index < fishCount; ++index) {
  const phase = index * 2.399963;
  actors.push([-4.5 + (index % 5) * 2.1, 2.4 + (index % 4) * 1.05,
    1.8 - (index % 3) * 0.8, 1.1 + (index % 4) * 0.07,
    1.3, 0.16, 0.10 + index * 0.002, phase,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
}
for (const name of ['body', 'fins']) {
  const mesh = new THREE.InstancedMesh(anatomy[name], new THREE.MeshStandardMaterial(), fishCount);
  for (let index = 0; index < fishCount; ++index) mesh.setMatrixAt(index, identity);
  exportMesh(mesh, [name === 'body' ? 11 : 12, 4], 0);
}
// Versioned little-endian, fixed-layout data; C++ validates counts and relationships.
const header = Buffer.alloc(32);
header.write('STWSCN1\0', 0, 'ascii');
const counts = [vertices.length / 32, indices.length, instances.length, objects.length, batches.length, actors.length];
for (let index = 0; index < counts.length; ++index) header.writeUInt32LE(counts[index], 8 + index * 4);
function floats(values) { return Buffer.from(new Float32Array(values).buffer); }
function integers(values) { return Buffer.from(new Uint32Array(values).buffer); }
const objectBytes = Buffer.alloc(objects.length * 32);
for (let index = 0; index < objects.length; ++index) {
  const object = objects[index], start = index * 32;
  objectBytes.writeUInt32LE(object.kind, start);
  objectBytes.writeUInt32LE(object.instance, start + 4);
  for (let axis = 0; axis < 3; ++axis) objectBytes.writeFloatLE(object.center[axis], start + 8 + axis * 4);
  objectBytes.writeFloatLE(object.radius, start + 20);
  objectBytes.writeInt32LE(object.actor, start + 24);
}
const data = Buffer.concat([header, floats(vertices), integers(indices), floats(instances.flat()),
  objectBytes, integers(batches.flat()), floats(actors.flat())]);
const compressed = gzipSync(data, { level: 9 });
writeFileSync(new URL('../assets/riverscape.swscene.gz', import.meta.url), compressed);
writeFileSync(new URL('../assets/riverscape-manifest.json', import.meta.url), JSON.stringify({
  source: 'chaseleantj/desktop-habitats', commit: '66e80ea1b6bafd25324c84c72067e750c1a1f334',
  counts: { vertices: counts[0], indices: counts[1], objects: counts[3], batches: counts[4], actors: counts[5] },
  decompressedBytes: data.length, compressedBytes: compressed.length, planting: planting.stats,
  omitted: ['contact-shadow image planes', 'backboard', 'browser host', 'Three.js runtime', 'CPU fish behavior', 'postprocessing'],
}, null, 2) + '\n');
console.log(JSON.stringify({ counts, bytes: data.length, compressed: compressed.length }));
