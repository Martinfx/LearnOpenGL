"""First-person view model of the incendiary bottle:
    python3 -I build_view.py <fp_knife.glb> [out.glb]   (default out: ../../fps/fp_molotov.glb)

Takes the professional arms rig of fp_knife.glb (DJMaesen, CC-BY 4.0, see ../../fps/CREDITS.md), drops the
knife and puts the bottle (bottle.py) in the right hand, held by the neck with the burning rag up:
- `idle` and `draw`: the knife rig's clips with the wrist turned (constant, so the fist holds the neck upright);
- `throw` (0.8 s): wind up over the shoulder, swing forward, the bottle leaves the hand at 0.42 s (bone
  `W_bottle` scales to nothing), follow through and the arm drops out of view; the game then plays `draw`.
  Keyed with a two-bone IK on the rig's own upper arm and forearm (shoulder fixed), the hand pitched by hand.
Bones added: `W_bottle` (child of the wrist, the bottle is rigidly skinned to it) and `W_rag` (the rag's
top, where the game puts the flame: `trail_bone "W_rag"` in molotov.weapon).
Engine convention like the other view models: eye at the origin, -Z forward, +Y up, metres.
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Matrix, Quaternion, Vector

args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
SRC = args[0]
OUT = args[1] if len(args) > 1 else os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'fps', 'fp_molotov.glb'))

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC, guess_original_bind_pose=False)
import bottle  # after the reset (it creates materials)
sc = bpy.context.scene
sc.render.fps = 24
arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
A = arm.matrix_world.copy()
Ainv = A.inverted()
ArotInv = Ainv.to_3x3().normalized()


def eng(x, y, z):
    """Engine view space (x right, y up, -z forward) -> Blender world (glTF import: x, -z, y)."""
    return Vector((x, -z, y))


def to_pose_dir(v):
    return (ArotInv @ v).normalized()


def to_pose(p):
    return Ainv @ p


def use(action, frame):
    arm.animation_data.action = action
    try:
        arm.animation_data.action_slot = action.slots[0]
    except Exception:
        pass
    sc.frame_set(frame)
    bpy.context.view_layer.update()


def sample(action):
    f0, f1 = [int(round(x)) for x in action.frame_range]
    frames = {}
    for f in range(f0, f1 + 1):
        use(action, f)
        frames[f] = {pb.name: pb.matrix_basis.copy() for pb in arm.pose.bones}
    return frames


src = {name: bpy.data.actions[name] for name in ('idle', 'draw')}
use(src['idle'], 0)
WR = 'R_wrist_027'
wrist0 = arm.pose.bones[WR].matrix.copy()  # pose space, idle frame 0, before turning the wrist

# Knife handle (where the fist closes) and the knife's axis, in the wrist's frame.
import numpy as np
dg = bpy.context.evaluated_depsgraph_get()
knife = bpy.data.objects['knife_knife_0']
ev = knife.evaluated_get(dg)
me = ev.to_mesh()
P = np.array([(Ainv @ (ev.matrix_world @ v.co))[:] for v in me.vertices])
ev.to_mesh_clear()
c = P.mean(0)
_, _, vt = np.linalg.svd(P - c)
ax = vt[0]
proj = (P - c) @ ax
palm = np.array(arm.pose.bones['R_palm_040'].matrix.translation[:])
if (palm - c) @ ax < 0:  # handle toward +ax (palm side), blade toward -ax
    ax = -ax
    proj = -proj
handle = c + ax * (proj.max() * 0.62)  # middle of the fist
blade_dir = Vector(-ax)                # the thumb side: where the rag will point
handle_local = wrist0.inverted() @ Vector(handle)
bladedir_local = (wrist0.to_3x3().inverted() @ blade_dir).normalized()

# Turn the wrist (constant, in its own frame) so the neck stands upright, tipped a little left and away.
want = to_pose_dir(eng(-0.22, 0.92, -0.32).normalized())
now = (wrist0.to_3x3() @ bladedir_local).normalized()
W = now.rotation_difference(want)
R0 = wrist0.to_quaternion()
Q = R0.inverted() @ W @ R0  # local offset: basis' = basis @ Q

# Source clips sampled, the wrist turned.
clips = {}
for name, act in src.items():
    frames = sample(act)
    for f, bones in frames.items():
        b = bones[WR]
        loc, rot, scl = b.decompose()
        bones[WR] = Matrix.LocRotScale(loc, rot @ Q, scl)
    clips[name] = frames

# Bones for the bottle and the rag.
bpy.context.view_layer.objects.active = arm
arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
eb_w = arm.data.edit_bones[WR]
eb = arm.data.edit_bones.new('W_bottle')
eb.head, eb.tail, eb.roll = eb_w.head.copy(), eb_w.tail.copy(), eb_w.roll
eb.parent = eb_w
eb.use_connect = False
eb.use_deform = True
bpy.ops.object.mode_set(mode='OBJECT')
for pb in arm.pose.bones:
    pb.rotation_mode = 'QUATERNION'


def key_clip(name, frames, extra=None):
    act = bpy.data.actions.new(name)
    arm.animation_data.action = act
    for f, bones in sorted(frames.items()):
        for bn, m in bones.items():
            pb = arm.pose.bones.get(bn)
            if pb is None:
                continue
            loc, rot, scl = m.decompose()
            pb.location, pb.rotation_quaternion, pb.scale = loc, rot, scl
            for p in ('location', 'rotation_quaternion', 'scale'):
                pb.keyframe_insert(p, frame=f)
    return act


# Idle and draw: the bottle in hand (W_bottle identity).
for name in ('idle', 'draw'):
    for f in clips[name]:
        clips[name][f]['W_bottle'] = Matrix()
new = {name: key_clip(name + '_m', clips[name]) for name in ('idle', 'draw')}

# The bottle where the fist is, at idle frame 0 (turned wrist).
use(new['idle'], 0)
wb = arm.pose.bones['W_bottle'].matrix.copy()
rest_wb = arm.data.bones['W_bottle'].matrix_local.copy()
grip = wb @ handle_local
up = (wb.to_3x3() @ bladedir_local).normalized()
toward_eye = (to_pose(Vector((0, 0, 0))) - grip)
side = (toward_eye - up * toward_eye.dot(up)).normalized()  # the label faces the eye
# Bottle frame: z = up, label centre (local angle 135 deg) toward the eye.
a = math.radians(135.0)
lab = Vector((math.cos(a), math.sin(a), 0.0))
x_axis = side
y_axis = up.cross(x_axis).normalized()
basis = Matrix((x_axis, y_axis, up)).transposed()  # columns
turn = Matrix.Rotation(-math.atan2(lab.y, lab.x), 3, 'Z')   # local label dir -> local +x
GRIP_Z = 0.238  # height on the bottle the fist holds (the neck)
bottle_to_pose = Matrix.Translation(grip) @ (basis @ turn).to_4x4() @ Matrix.Translation(Vector((0, 0, -GRIP_Z)))
# Scale: bottle.py is in metres; the armature's pose space is in its own units.
unit = 1.0 / A.to_scale()[0]
bottle_to_pose = Matrix.Translation(grip) @ (basis @ turn).to_4x4() @ Matrix.Scale(unit, 4) @ Matrix.Translation(Vector((0, 0, -GRIP_Z)))
to_rest = rest_wb @ wb.inverted()

objs = bottle.build()
for o in objs:
    for m in o.modifiers:
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_apply(modifier=m.name)
bpy.ops.object.select_all(action='DESELECT')
for o in objs:
    o.select_set(True)
bpy.context.view_layer.objects.active = objs[0]
bpy.ops.object.join()
bot = objs[0]
bot.name = 'molotov_bottle'
bot.data.transform(to_rest @ bottle_to_pose)
bot.parent = arm
bot.matrix_parent_inverse = Matrix()
bot.matrix_world = A.copy()
vg = bot.vertex_groups.new(name='W_bottle')
vg.add(range(len(bot.data.vertices)), 1.0, 'REPLACE')
md = bot.modifiers.new('Armature', 'ARMATURE')
md.object = arm

# The rag's top as a bone (the flame's anchor).
rag_tip_pose = bottle_to_pose @ Vector((0.0, 0.0, bottle.RAG_TOP - 0.01))
rag_tip_rest = to_rest @ rag_tip_pose
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='EDIT')
eb = arm.data.edit_bones.new('W_rag')
eb.head = rag_tip_rest
eb.tail = rag_tip_rest + (to_rest.to_3x3() @ up) * (0.03 * unit)
eb.parent = arm.data.edit_bones['W_bottle']
eb.use_connect = False
eb.use_deform = False
bpy.ops.object.mode_set(mode='OBJECT')
arm.pose.bones['W_rag'].rotation_mode = 'QUATERNION'

# ---------------------------------------------------------------- throw (two-bone IK)
UP, FORE = 'R_arm_025', 'R_elbow_026'
use(new['idle'], 0)
idle0 = {pb.name: pb.matrix_basis.copy() for pb in arm.pose.bones}
M_u0 = arm.pose.bones[UP].matrix.copy()
M_f0 = arm.pose.bones[FORE].matrix.copy()
M_w0 = arm.pose.bones[WR].matrix.copy()
S = M_u0.translation.copy()
E0 = M_f0.translation.copy()
T0 = M_w0.translation.copy()
l1 = (E0 - S).length
l2 = (T0 - E0).length
print('arm lengths (m):', round(l1 / unit, 3), round(l2 / unit, 3), 'wrist at', [round(v, 3) for v in (A @ T0)])


def rot_about(q, pivot):
    return Matrix.Translation(pivot) @ q.to_matrix().to_4x4() @ Matrix.Translation(-pivot)


def pose_for(target_eng, pole_eng, pitch_deg):
    T = to_pose(eng(*target_eng))
    pole = to_pose_dir(eng(*pole_eng))
    d = min((T - S).length, (l1 + l2) * 0.985)
    dirST = (T - S).normalized()
    T = S + dirST * d
    a = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
    h = math.sqrt(max(l1 * l1 - a * a, 0.0))
    p = (pole - dirST * pole.dot(dirST)).normalized()
    E = S + dirST * a + p * h
    q1 = (E0 - S).normalized().rotation_difference((E - S).normalized())
    R1 = rot_about(q1, S)
    Ef = R1 @ E0
    Wf = R1 @ T0
    q2 = (Wf - Ef).normalized().rotation_difference((T - Ef).normalized())
    R2 = rot_about(q2, Ef)
    Mu = R1 @ M_u0
    Mf = R2 @ R1 @ M_f0
    pitch = Quaternion(to_pose_dir(Vector((1, 0, 0))), math.radians(pitch_deg))
    loc, rot, scl = M_w0.decompose()
    Mw = Matrix.LocRotScale(T, pitch @ rot, scl)
    # matrices -> bases (parents first)
    use(new['idle'], 0)
    for bn, m in idle0.items():
        arm.pose.bones[bn].matrix_basis = m
    bpy.context.view_layer.update()
    out = {}
    for bn, M in ((UP, Mu), (FORE, Mf), (WR, Mw)):
        arm.pose.bones[bn].matrix = M
        bpy.context.view_layer.update()
        out[bn] = arm.pose.bones[bn].matrix_basis.copy()
    return out


# time (s), wrist target (engine view space), elbow pole direction, hand pitch (+ = rag tipped back)
KEYS = [
    (0.00, (0.204, -0.092, -0.467), (1.0, -1.0, 0.2), 0),
    (0.10, (0.235, -0.030, -0.400), (1.0, -0.6, 0.3), 18),
    (0.24, (0.255, 0.105, -0.215), (1.0, 0.1, 0.6), 58),
    (0.32, (0.215, 0.120, -0.300), (1.0, 0.0, 0.4), 40),
    (0.40, (0.110, 0.060, -0.470), (1.0, -0.4, -0.2), -20),
    (0.46, (0.080, -0.030, -0.500), (1.0, -0.8, -0.2), -40),
    (0.58, (0.110, -0.250, -0.440), (1.0, -1.0, 0.0), -50),
    (0.80, (0.200, -0.560, -0.260), (1.0, -1.0, 0.2), -50),
]
RELEASE = 0.42
fps = sc.render.fps
frames = {}
for t, target, pole, pitch in KEYS:
    f = int(round(t * fps))
    bones = dict(idle0)
    bones.update(pose_for(target, pole, pitch))
    bones['W_bottle'] = Matrix()
    bones['W_rag'] = Matrix()
    frames[f] = bones
# In between keys: interpolate the IK targets too (smooth arcs, not straight bone blends).
dense = {}
for i in range(len(KEYS) - 1):
    t0, p0, o0, a0 = KEYS[i]
    t1, p1, o1, a1 = KEYS[i + 1]
    f0, f1 = int(round(t0 * fps)), int(round(t1 * fps))
    for f in range(f0, f1):
        u = (f - f0) / max(f1 - f0, 1)
        s = u * u * (3 - 2 * u)
        lerp = lambda a, b: tuple(x + (y - x) * s for x, y in zip(a, b))
        bones = dict(idle0)
        bones.update(pose_for(lerp(p0, p1), lerp(o0, o1), a0 + (a1 - a0) * s))
        dense[f] = bones
dense[int(round(KEYS[-1][0] * fps))] = frames[int(round(KEYS[-1][0] * fps))]
rf = int(math.floor(RELEASE * fps))
for f, bones in dense.items():
    bones['W_bottle'] = Matrix() if f <= rf else Matrix.Scale(1e-4, 4)
    bones['W_rag'] = Matrix()
throw = key_clip('throw_m', dense)
# draw/idle keep the bottle at full size (keys written by key_clip above for W_bottle = identity)

# Clean up: the knife, its empties and the old clips go; the new clips get their names.
for o in list(bpy.data.objects):
    if o.name.startswith('knife') or o.name == 'Icosphere':
        bpy.data.objects.remove(o)
keep = {'idle_m': 'idle', 'draw_m': 'draw', 'throw_m': 'throw'}
for a in list(bpy.data.actions):
    if a.name not in keep:
        bpy.data.actions.remove(a)
for a in bpy.data.actions:
    a.name = keep[a.name]
use(bpy.data.actions['idle'], 0)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_animations=True, export_animation_mode='ACTIONS',
                          export_force_sampling=True, export_cameras=False, export_lights=False, export_image_format='JPEG',
                          export_jpeg_quality=88, export_yup=True, export_skins=True, export_def_bones=False,
                          export_optimize_animation_size=False)
print('wrote', OUT)
