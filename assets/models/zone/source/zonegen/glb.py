"""Minimal glTF 2.0 binary writer for zonegen meshes.

Textures are referenced by relative URI (shared 1K sets in assets/models/zone/textures/),
so many models reuse the same files on disk and the .glb files stay small.
"""
import json
import struct
import numpy as np


def write_glb(path, mesh, tex_rel_dir='textures', name='model'):
    """mesh: zonegen.mesh.Mesh. Material dict keys:
       tex  - texture set name (files <tex>_albedo.jpg, _normal.jpg, _orm.jpg) or None
       color - linear RGB factor, rough, metal - factors (multiplied with the ORM map)."""
    bin_chunks = []
    offset = 0
    buffer_views, accessors, meshes_prims, materials, textures, images = [], [], [], [], [], []
    image_index = {}

    def add_view(data, target):
        nonlocal offset
        pad = (-len(data)) % 4
        bin_chunks.append(data + b'\0' * pad)
        buffer_views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data), 'target': target})
        offset += len(data) + pad
        return len(buffer_views) - 1

    def tex_index(file):
        if file not in image_index:
            images.append({'uri': f'{tex_rel_dir}/{file}'})
            textures.append({'source': len(images) - 1, 'sampler': 0})
            image_index[file] = len(textures) - 1
        return image_index[file]

    mat_index = {}
    for mname, pr in mesh.prims.items():
        if not pr.idx:
            continue
        m = mesh.materials[mname]
        if mname not in mat_index:
            col = list(m.get('color', (1, 1, 1))) + [1.0]
            pbr = {'baseColorFactor': col, 'metallicFactor': m.get('metal', 0.0), 'roughnessFactor': m.get('rough', 1.0)}
            mat = {'name': mname, 'pbrMetallicRoughness': pbr}
            t = m.get('tex')
            if t and m.get('decal'):      # grime decals: RGBA png, alpha-blended over the wall
                pbr['baseColorTexture'] = {'index': tex_index(f'{t}_albedo.png')}
                mat['alphaMode'] = 'BLEND'
            elif t:
                pbr['baseColorTexture'] = {'index': tex_index(f'{t}_albedo.jpg')}
                pbr['metallicRoughnessTexture'] = {'index': tex_index(f'{t}_orm.jpg')}
                mat['normalTexture'] = {'index': tex_index(f'{t}_normal.jpg'), 'scale': m.get('normal', 1.0)}
                mat['occlusionTexture'] = {'index': tex_index(f'{t}_orm.jpg')}
            if 'emissive' in m:
                mat['emissiveFactor'] = list(m['emissive'])
            if 'alpha' in m:            # glass: blended, alpha in the base colour factor
                col[3] = m['alpha']
                mat['alphaMode'] = 'BLEND'
                mat['doubleSided'] = True
            materials.append(mat)
            mat_index[mname] = len(materials) - 1
        pos = np.asarray(pr.pos, dtype=np.float32)
        nrm = np.asarray(pr.nrm, dtype=np.float32)
        uv = np.asarray(pr.uv, dtype=np.float32).copy()
        uv[:, 1] = -uv[:, 1]
        idx = np.asarray(pr.idx, dtype=np.uint32)
        pv = add_view(pos.tobytes(), 34962)
        accessors.append({'bufferView': pv, 'componentType': 5126, 'count': len(pos), 'type': 'VEC3',
                          'min': pos.min(axis=0).tolist(), 'max': pos.max(axis=0).tolist()})
        pa = len(accessors) - 1
        nv = add_view(nrm.tobytes(), 34962)
        accessors.append({'bufferView': nv, 'componentType': 5126, 'count': len(nrm), 'type': 'VEC3'})
        na = len(accessors) - 1
        tv = add_view(uv.tobytes(), 34962)
        accessors.append({'bufferView': tv, 'componentType': 5126, 'count': len(uv), 'type': 'VEC2'})
        ta = len(accessors) - 1
        iv = add_view(idx.tobytes(), 34963)
        accessors.append({'bufferView': iv, 'componentType': 5125, 'count': len(idx), 'type': 'SCALAR'})
        ia = len(accessors) - 1
        meshes_prims.append({'attributes': {'POSITION': pa, 'NORMAL': na, 'TEXCOORD_0': ta},
                             'indices': ia, 'material': mat_index[mname]})

    gltf = {
        'asset': {'version': '2.0', 'generator': 'zonegen (FPS engine, CC0)'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [{'mesh': 0, 'name': name}],
        'meshes': [{'name': name, 'primitives': meshes_prims}],
        'materials': materials,
        'buffers': [{'byteLength': offset}],
        'bufferViews': buffer_views,
        'accessors': accessors,
    }
    if textures:
        gltf['textures'] = textures
        gltf['images'] = images
        gltf['samplers'] = [{'magFilter': 9729, 'minFilter': 9987, 'wrapS': 10497, 'wrapT': 10497}]
    js = json.dumps(gltf, separators=(',', ':')).encode()
    js += b' ' * ((-len(js)) % 4)
    binary = b''.join(bin_chunks)
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(js), 0x4E4F534A))
        f.write(js)
        f.write(struct.pack('<II', len(binary), 0x004E4942))
        f.write(binary)
