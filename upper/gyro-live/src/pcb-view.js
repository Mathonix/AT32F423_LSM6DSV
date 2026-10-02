import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

export async function createPCBView(stage, onChange, onAvailability) {
  const canvas = document.createElement('canvas');
  canvas.id = 'pcbModel'; canvas.setAttribute('aria-label', '板卡真实三维模型，可拖动视角和缩放');
  stage.prepend(canvas);
  let renderer;
  try { renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false }); }
  catch (error) { canvas.remove(); throw error; }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.setSize(720, 410, false);
  renderer.setClearColor(0x0d1623);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.35;
  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(2 * Math.atan(1 / 3.1) * 180 / Math.PI, 720 / 410, .1, 40);
  camera.up.set(0, 0, 1);
  const az = -125 * Math.PI / 180, el = 32 * Math.PI / 180;
  const home = new THREE.Vector3(6.2 * Math.cos(el) * Math.cos(az), 6.2 * Math.cos(el) * Math.sin(az), 6.2 * Math.sin(el));
  camera.position.copy(home); camera.lookAt(0, 0, 0);
  const controls = new OrbitControls(camera, canvas);
  controls.enablePan = false; controls.minDistance = 3.3; controls.maxDistance = 11;
  controls.addEventListener('change', onChange);
  scene.add(new THREE.HemisphereLight(0xe7f2ff, 0x607080, 2.6));
  for (const [position, color, intensity] of [[[2, -4, 6], 0xffffff, 3], [[-4, 3, 2], 0x8cbcff, 2], [[1, 4, -3], 0xffffff, 1.4]]) {
    const light = new THREE.DirectionalLight(color, intensity); light.position.set(...position); scene.add(light);
  }
  const grid = new THREE.GridHelper(4, 10, 0x30455e, 0x1f3350);
  grid.rotation.x = Math.PI / 2; grid.position.z = -1.3; scene.add(grid);
  const board = new THREE.Group(); board.matrixAutoUpdate = false; scene.add(board);
  let metadata, lost = false;
  try {
    const gltf = await new GLTFLoader().loadAsync('/models/pcb.glb?v=20261001d');
    // PCB installation: CAD +X is sensor +Y, CAD +Y is sensor -X.
    // A proper rotation keeps Euler and quaternion paths identical.
    gltf.scene.rotation.z = Math.PI / 2;
    board.add(gltf.scene); metadata = gltf.parser.json.extras;
    if (!metadata?.bounds || metadata.triangles < 1) throw new Error('PCB metadata missing');
  } catch (error) { controls.dispose(); renderer.dispose(); canvas.remove(); throw error; }
  canvas.addEventListener('webglcontextlost', event => {
    event.preventDefault(); lost = true; canvas.hidden = true; stage.classList.remove('model-ready'); onAvailability(false);
  });
  canvas.addEventListener('webglcontextrestored', () => {
    lost = false; canvas.hidden = false; stage.classList.add('model-ready'); onAvailability(true);
  });
  const vector = new THREE.Vector3();
  const corners = [];
  for (const x of [metadata.bounds.min[0], metadata.bounds.max[0]])
    for (const y of [metadata.bounds.min[1], metadata.bounds.max[1]])
      for (const z of [metadata.bounds.min[2], metadata.bounds.max[2]]) corners.push(new THREE.Vector3(-y, x, z));
  return {
    get ready() { return !lost; },
    project(point) {
      vector.set(...point).project(camera);
      return [(vector.x + 1) * 360, (1 - vector.y) * 205];
    },
    render(R, stale) {
      board.matrix.set(R[0][0], R[0][1], R[0][2], 0, R[1][0], R[1][1], R[1][2], 0, R[2][0], R[2][1], R[2][2], 0, 0, 0, 0, 1);
      board.matrixWorldNeedsUpdate = true;
      renderer.toneMappingExposure = stale ? .65 : 1.35;
      renderer.render(scene, camera);
      return corners.map(p => {
        vector.copy(p).applyMatrix4(board.matrix).project(camera);
        return [(vector.x + 1) * 360, (1 - vector.y) * 205];
      });
    },
    reset() { controls.target.set(0, 0, 0); camera.position.copy(home); controls.update(); onChange(); },
    state() { return { ready: !lost, source: metadata.source, sourceSha256: metadata.sourceSha256,
      componentMeshes: metadata.componentMeshes, triangles: metadata.triangles, materials: metadata.materialCount,
      boardDimensionsMm: metadata.boardDimensionsMm, drawCalls: renderer.info.render.calls, renderedTriangles: renderer.info.render.triangles,
      cadToBody: [[0,-1,0],[1,0,0],[0,0,1]],
      cadWorldMatrix: board.children[0].matrixWorld.elements.slice(),
      matrix: board.matrix.elements.slice(), camera: camera.position.toArray() }; },
  };
}
