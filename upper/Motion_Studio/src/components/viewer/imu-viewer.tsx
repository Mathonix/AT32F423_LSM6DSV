import { Canvas, useThree } from "@react-three/fiber";
import { useEffect, useMemo } from "react";
import * as THREE from "three";
import type { TelemetrySample } from "@/types/device";

function CameraRig({ view }: { view: "perspective" | "front" | "top" | "side" }) {
  const { camera } = useThree();
  useEffect(() => {
    const positions: Record<typeof view, [number, number, number]> = {
      perspective: [3.2, 2.4, 3.6],
      front: [0, 0.8, 5],
      top: [0, 5, 0.01],
      side: [5, 0.8, 0],
    };
    camera.position.set(...positions[view]);
    camera.lookAt(0, 0, 0);
    camera.updateProjectionMatrix();
  }, [camera, view]);
  return null;
}

function Board({ sample, reference }: { sample?: TelemetrySample; reference?: [number, number, number, number] }) {
  const quaternion = useMemo(() => {
    if (!sample) return new THREE.Quaternion();
    const [w, x, y, z] = sample.quaternion;
    const current = new THREE.Quaternion(x, y, z, w).normalize();
    if (!reference) return current;
    const [rw, rx, ry, rz] = reference;
    return new THREE.Quaternion(rx, ry, rz, rw).normalize().invert().multiply(current).normalize();
  }, [sample, reference]);

  return (
    <group quaternion={quaternion}>
      <mesh castShadow receiveShadow>
        <boxGeometry args={[2.2, 0.16, 1.25]} />
        <meshStandardMaterial color="#25323a" roughness={0.55} metalness={0.15} />
      </mesh>
      <mesh position={[0, 0.12, 0]}>
        <boxGeometry args={[0.72, 0.08, 0.56]} />
        <meshStandardMaterial color="#3c515d" roughness={0.42} />
      </mesh>
      <mesh position={[0.82, 0.11, 0.34]}>
        <boxGeometry args={[0.2, 0.06, 0.2]} />
        <meshStandardMaterial color="#2abbb2" roughness={0.38} />
      </mesh>
      <axesHelper args={[1.65]} />
    </group>
  );
}

export function ImuViewer({ sample, reference, view = "perspective" }: { sample?: TelemetrySample; reference?: [number, number, number, number]; view?: "perspective" | "front" | "top" | "side" }) {
  return (
    <Canvas camera={{ position: [3.2, 2.4, 3.6], fov: 38 }} dpr={[1, 1.75]} gl={{ antialias: true }}>
      <color attach="background" args={["#0f1317"]} />
      <ambientLight intensity={1.15} />
      <directionalLight position={[4, 6, 3]} intensity={2.1} />
      <directionalLight position={[-3, 2, -4]} intensity={0.65} />
      <gridHelper args={[10, 20, "#26343c", "#1a2329"]} position={[0, -1.05, 0]} />
      <Board sample={sample} reference={reference} />
      <CameraRig view={view} />
    </Canvas>
  );
}
