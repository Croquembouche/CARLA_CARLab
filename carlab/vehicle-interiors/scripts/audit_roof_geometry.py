import bpy
import json
from pathlib import Path

ROOT = Path('/mnt/simulations/vehicle-interiors')
bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'Lincoln_MKZ_Interior_Final.blend'))

result = {}
for obj in bpy.context.scene.objects:
    if obj.type != 'MESH':
        continue
    world = obj.matrix_world
    roof_vertices = [world @ vertex.co for vertex in obj.data.vertices if (world @ vertex.co).z > 1.30]
    if not roof_vertices:
        continue
    roof_faces = []
    material_counts = {}
    for polygon in obj.data.polygons:
        points = [world @ obj.data.vertices[index].co for index in polygon.vertices]
        if max(point.z for point in points) <= 1.30:
            continue
        roof_faces.append(polygon)
        name = obj.data.materials[polygon.material_index].name if polygon.material_index < len(obj.data.materials) and obj.data.materials[polygon.material_index] else '<none>'
        material_counts[name] = material_counts.get(name, 0) + 1
    result[obj.name] = {
        'vertices_above_1_30': len(roof_vertices),
        'faces_above_1_30': len(roof_faces),
        'roof_bounds': {
            'min': [min(point[i] for point in roof_vertices) for i in range(3)],
            'max': [max(point[i] for point in roof_vertices) for i in range(3)],
        },
        'roof_material_faces': material_counts,
    }

(ROOT / 'roof-geometry-audit.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
