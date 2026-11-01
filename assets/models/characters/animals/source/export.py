import bpy,sys
bpy.ops.wm.open_mainfile(filepath=sys.argv[1])
for o in bpy.data.objects: o.select_set(o.type in ('ARMATURE','MESH'))
bpy.ops.export_scene.gltf(filepath=sys.argv[2],export_format='GLB',use_selection=True,export_yup=True,export_apply=True,
    export_image_format='JPEG',export_jpeg_quality=88,export_animations=True,export_animation_mode='ACTIONS',
    export_force_sampling=True,export_frame_range=False,export_anim_single_armature=True,export_def_bones=False,export_optimize_animation_size=False)
