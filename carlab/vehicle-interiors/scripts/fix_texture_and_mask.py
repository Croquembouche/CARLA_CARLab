import unreal as u
D='/Game/VehicleInteriors/Lincoln/'
for name in ['T_MKZ_Ebony_BaseColor','T_MKZ_Ebony_ORM']:
 tex=u.load_asset(D+name)
 tex.set_editor_property('virtual_texture_streaming',False)
 assert u.EditorAssetLibrary.save_loaded_asset(tex,False)
m=u.load_asset(D+'M_MKZ_Interior_PBR')
u.MaterialEditingLibrary.recompile_material(m)
assert u.EditorAssetLibrary.save_loaded_asset(m,False)
h=u.load_asset(D+'M_Hidden_StockCabin')
h.set_editor_property('used_with_nanite',True)
u.MaterialEditingLibrary.recompile_material(h)
assert u.EditorAssetLibrary.save_loaded_asset(h,False)
