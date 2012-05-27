// 3D World - Drawing Code
// by Frank Gennari
// 3/10/02
#include "3DWorld.h"
#include "mesh.h"
#include "textures_3dw.h"
#include "dynamic_particle.h"
#include "physics_objects.h"
#include "gl_ext_arb.h"
#include "shaders.h"


bool const DYNAMIC_SMOKE_SHADOWS = 1; // slower, but looks nice
unsigned const MAX_CFILTERS      = 10;
float const NDIV_SCALE           = 1.6;
float const CLOUD_WIND_SPEED     = 0.00015;


struct sky_pos_orient {

	point center;
	float radius, radius_inv, dx, dy;
	sky_pos_orient(point const &c, float r, float dx_, float dy_)
		: center(c), radius(r), radius_inv(1.0/radius), dx(dx_), dy(dy_) {assert(radius > 0.0);}
};


// Global Variables
float sun_radius, moon_radius, earth_radius, brightness(1.0);
colorRGBA cur_ambient(BLACK), cur_diffuse(BLACK);
point sun_pos, moon_pos;
point gl_light_positions[8] = {all_zeros};
point const earth_pos(-15.0, -8.0, 21.0);
sky_pos_orient cur_spo(point(0,0,0),1,0,0);
vector3d up_norm(plus_z);
vector<camera_filter> cfilters;
pt_line_drawer bubble_pld;


extern GLUquadricObj* quadric;
extern bool have_sun, using_lightmap, has_dl_sources, has_dir_lights, smoke_exists, two_sided_lighting;
extern bool group_back_face_cull, have_indir_smoke_tex, create_voxel_landscape;
extern int is_cloudy, iticks, display_mode, show_fog, num_groups, island;
extern int window_width, window_height, game_mode, enable_fsource, draw_model, camera_mode;
extern unsigned smoke_tid, dl_tid, num_stars;
extern float zmin, light_factor, fticks, perspective_fovy, perspective_nclip, cobj_z_bias;
extern float temperature, atmosphere, zbottom, indir_vert_offset;
extern point light_pos, mesh_origin, flow_source, surface_pos;
extern vector3d wind;
extern colorRGB const_indir_color;
extern colorRGBA bkg_color, sun_color;
extern vector<spark_t> sparks;
extern vector<star> stars;
extern vector<beam3d> beams;
extern obj_group obj_groups[];
extern coll_obj_group coll_objects;
extern obj_type object_types[];
extern obj_vector_t<bubble> bubbles;
extern obj_vector_t<particle_cloud> part_clouds;
extern cloud_manager_t cloud_manager;
extern obj_vector_t<fire> fires;
extern obj_vector_t<decal_obj> decals;
extern cube_t cur_smoke_bb;
extern vector<portal> portals;
extern vector<obj_draw_group> obj_draw_groups;



void set_fill_mode() {
	glPolygonMode(GL_FRONT_AND_BACK, ((draw_model == 0) ? GL_FILL : GL_LINE));
}

int get_universe_ambient_light() {
	return ((world_mode == WMODE_UNIVERSE) ? GL_LIGHT1 : GL_LIGHT3);
}


void set_colors_and_enable_light(int light, float const ambient[4], float const diffuse[4]) {

	glEnable(light);
	glLightfv(light, GL_AMBIENT, ambient);
	glLightfv(light, GL_DIFFUSE, diffuse);
}


void clear_colors_and_disable_light(int light) {

	float const ad[4] = {0.0, 0.0, 0.0, 0.0};
	glDisable(light);
	glLightfv(light, GL_AMBIENT, ad);
	glLightfv(light, GL_DIFFUSE, ad);
}


void set_gl_light_pos(int light, point const &pos, float w) {

	assert(light >= GL_LIGHT0 && light <= GL_LIGHT7);
	float position[4];
	for (unsigned i = 0; i < 3; ++i) position[i] = pos[i];
	position[3] = w;
	glLightfv(light, GL_POSITION, position);
	gl_light_positions[light - GL_LIGHT0] = pos;
}


void get_shadowed_color(colorRGBA &color_a, point const &pos, bool &is_shadowed, bool precip, bool no_dynamic) {

	if ((using_lightmap || create_voxel_landscape || (!no_dynamic && has_dl_sources)) && color_a != BLACK) { // somewhat slow
		float const val(get_indir_light(color_a, (pos + vector3d(0.0, 0.0, 0.01)), no_dynamic, (is_shadowed || precip), NULL, NULL)); // get above mesh
		if (precip && val < 1.0) is_shadowed = 1; // if precip, imply shadow status from indirect light value
	}
}


// Note: incorrect if there is both a sun and a moon
bool pt_is_shadowed(point const &pos, int light, float radius, int cid, bool fast, bool use_mesh) {

	if (use_mesh) {
		int const xpos(get_ypos(pos.x)), ypos(get_ypos(pos.y));
		if (point_outside_mesh(xpos, ypos)) return 0;

		if ((pos.z - 1.5*radius) < mesh_height[ypos][xpos]) {
			//if (is_mesh_disabled(xpos, ypos)) return 0; // assuming not drawing the mesh means it's underneath a cobj
			return ((shadow_mask[light][ypos][xpos] & SHADOWED_ALL) != 0);
		}
		if (fast) return (is_shadowed_lightmap(pos)); // use the precomputed lightmap value
	}
	return (!is_visible_to_light_cobj(pos, light, radius, cid, 0));
}


void set_color_alpha(colorRGBA color, float alpha) {

	color.alpha *= alpha;
	colorRGBA(0.0, 0.0, 0.0, color.alpha).do_glColor(); // sets alpha component
	set_color_a(BLACK);
	set_color_d(color);
}


template class pt_line_drawer_t<color_wrapper      >;
template class pt_line_drawer_t<color_wrapper_float>;


template<typename cwt> void pt_line_drawer_t<cwt>::add_textured_pt(point const &v, colorRGBA c, int tid) {

	if (tid >= 0) c = c.modulate_with(texture_color(tid));
	vector3d const view_dir(get_camera_pos(), v);
	add_pt(v, view_dir, c);
}


template<typename cwt> void pt_line_drawer_t<cwt>::add_textured_line(point const &v1, point const &v2, colorRGBA c, int tid) {

	if (tid >= 0) c = c.modulate_with(texture_color(tid));
	vector3d view_dir(get_camera_pos(), (v1 + v2)*0.5);
	orthogonalize_dir(view_dir, (v2 - v1), view_dir, 0);
	add_line(v1, view_dir, c, v2, view_dir, c);
}


template<typename cwt> void pt_line_drawer_t<cwt>::vnc_cont::draw(int type) const {
	
	if (empty()) return; // nothing to do
	glVertexPointer(3, GL_FLOAT,     sizeof(vnc), &(front().v));
	glNormalPointer(   GL_FLOAT,     sizeof(vnc), &(front().n));
	glColorPointer( 4, cwt::gl_type, sizeof(vnc), &(front().c));
	glDrawArrays(type, 0, (unsigned)size());
}


template<typename cwt> void pt_line_drawer_t<cwt>::draw() const {
		
	if (points.empty() && lines.empty()) return;
	GLboolean const col_mat_en(glIsEnabled(GL_COLOR_MATERIAL));
	assert(!(lines.size() & 1));
	assert((triangles.size() % 3) == 0);
	if (!col_mat_en) glEnable(GL_COLOR_MATERIAL);
	set_array_client_state(1, 0, 1, 1);
	points.draw(GL_POINTS);
	lines.draw(GL_LINES);
	triangles.draw(GL_TRIANGLES);
	if (!col_mat_en) glDisable(GL_COLOR_MATERIAL);
	//cout << "mem: " << get_mem() << endl;
}


void quad_batch_draw::add_quad_vect(vector<vert_norm> const &points, colorRGBA const &color) {
	
	assert(!(points.size() & 3)); // must be a multiple of 4
	float const tcx[4] = {0,1,1,0}, tcy[4] = {0,0,1,1}; // 00 10 11 01
	color_wrapper cw;
	cw.set_c3(color);

	for (unsigned i = 0; i < points.size(); ++i) {
		verts.push_back(vert_norm_tc_color(points[i].v, points[i].n, tcx[i&3], tcy[i&3], cw.c));
	}
	unsigned const batch_size(4096);
	if (size() > batch_size) draw_and_clear();
}


void quad_batch_draw::draw() const {
	
	if (verts.empty()) return;
	assert(!(verts.size() & 3)); // must be a multiple of 4
	verts.front().set_state();
	glDrawArrays(GL_QUADS, 0, (unsigned)size());
}


void vert_norm_tc_color::set_vbo_arrays(unsigned stride_mult) {

	assert(stride_mult > 0);
	set_array_client_state(1, 1, 1, 1);
	unsigned const stride(stride_mult*sizeof(vert_norm_tc_color));
	glVertexPointer  (3, GL_FLOAT,         stride, (void *)(0));
	glNormalPointer  (   GL_FLOAT,         stride, (void *)(sizeof(point)));
	glTexCoordPointer(2, GL_FLOAT,         stride, (void *)(sizeof(vert_norm)));
	glColorPointer   (3, GL_UNSIGNED_BYTE, stride, (void *)(sizeof(vert_norm_tc)));
}


void vert_norm_tc_color::set_state(unsigned stride_mult) const {
	
	assert(stride_mult > 0);
	set_array_client_state(1, 1, 1, 1);
	unsigned const stride(stride_mult*sizeof(*this));
	glVertexPointer  (3, GL_FLOAT,         stride, &v);
	glNormalPointer  (   GL_FLOAT,         stride, &n);
	glTexCoordPointer(2, GL_FLOAT,         stride, &t);
	glColorPointer   (3, GL_UNSIGNED_BYTE, stride, &c);
}


void vert_color::set_state(unsigned vbo) const { // typically called on element 0
	
	unsigned const stride(sizeof(*this));
	set_array_client_state(1, 0, 0, 1);
	glVertexPointer(3, GL_FLOAT,         stride, (vbo ? (void *)0             : &v));
	glColorPointer (4, GL_UNSIGNED_BYTE, stride, (vbo ? (void *)sizeof(point) : &c));
}


void draw_camera_weapon(bool want_has_trans) {

	if (!game_mode || weap_has_transparent(CAMERA_ID) != want_has_trans) return;
	shader_t s;
	colorRGBA const orig_fog_color(setup_smoke_shaders(s, 0.0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 1));
	draw_weapon_in_hand(-1);
	end_smoke_shaders(s, orig_fog_color);
}


void draw_shadow_volume(point const &pos, point const &lpos, float radius, int &inverts) {

	// test for camera inside of cylinder and invert stencil
	vector3d v1(pos - lpos), v2(pos - get_camera_pos());
	float const dotp(dot_product(v1, v2)), val(v1.mag_sq()), length2(sqrt(val));
	if (dotp < 0.0 && (v2 - v1*(dotp/val)).mag_sq() < radius*radius) ++inverts;
	v1 /= length2;
	float const length((zmin - pos.z)/v1.z + radius), radius2(radius*((length + length2)/length2));
	draw_trunc_cone(pos, v1, length, (radius + SMALL_NUMBER), (radius2 + SMALL_NUMBER), 0);
}


// fast and good quality but has other problems:
// 1. slow for many lights (especially double pass mode)
// 2. camera shadow near clip (single pass mode)
// 3. double shadows cancel (single pass mode)
// 4. back faces of objects are double shadowed
// 5. somewhat incorrect for multiple colored lights
int draw_shadowed_objects(int light) {

	int inverts(0);
	point lpos;
	int const shadow_bit(1 << light);
	if (!get_light_pos(lpos, light)) return 0;

	for (int i = 0; i < num_groups; ++i) {
		obj_group const &objg(obj_groups[i]);
		if (!objg.temperature_ok() || !objg.large_radius()) continue;
		float const radius(object_types[objg.type].radius);

		for (unsigned j = 0; j < objg.end_id; ++j) {
			dwobject const &obj(objg.get_obj(j));
			if (obj.disabled()) continue;
			if ((obj.flags & (CAMERA_VIEW | SHADOWED)) || !(obj.shadow & shadow_bit)) continue;
			draw_shadow_volume(obj.pos, lpos, radius, inverts);
		} // for j
	} // for i
	if (display_mode & 0x0200) d_part_sys.add_stencil_shadows(lpos, inverts);
	// loop through cylinders of tree now...or maybe not
	return inverts;
}


void set_specular(float specularity, float shininess) {

	static float last_shiny(-1.0), last_spec(-1.0);
	if (is_cloudy && world_mode != WMODE_UNIVERSE) specularity *= 0.5;

	if (specularity != last_spec) { // This materialfv stuff seems to take some time, so only set if changed since last call
		float mat_specular[]  = {specularity, specularity, specularity, 1.0};
		glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR,  mat_specular);
		last_spec = specularity;
	}
	if (shininess != last_shiny) {
		float mat_shininess[] = {max(0.0f, min(128.0f, shininess))};
		glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
		last_shiny = shininess;
    }
}


void calc_cur_ambient_diffuse() {

	float a[4], d[4], lval[4];
	unsigned ncomp(0);
	cur_ambient = cur_diffuse = BLACK;

	for (unsigned i = 0; i < 8; ++i) { // max of 8 lights (GL_LIGHT0 - GL_LIGHT7): sun, moon, lightning
		int const light(GL_LIGHT0 + i); // should be sequential

		if (glIsEnabled(light)) {
			float atten(1.0);
			glGetLightfv(light, GL_AMBIENT, a);
			glGetLightfv(light, GL_DIFFUSE, d);
			glGetLightfv(light, GL_POSITION, lval);
			if (lval[3] != 0.0) glGetLightfv(light, GL_CONSTANT_ATTENUATION, &atten); // point light source only
			assert(atten > 0.0);
			UNROLL_3X(cur_ambient[i_] += a[i_]/atten; cur_diffuse[i_] += d[i_]/atten;)
			//cout << "A: "; cur_ambient.print(); cout << "  D: "; cur_diffuse.print(); cout << endl;
			++ncomp;
		}
	}
	if (ncomp > 0) {
		float const cscale(0.5 + 0.5/ncomp);
		cur_ambient      *= cscale; // only really valid for sun and moon
		cur_diffuse      *= cscale;
		cur_ambient.alpha = 1.0;
		cur_diffuse.alpha = 1.0;
	}
}


void upload_mvm_to_shader(shader_t &s, char const *const var_name) {

	float mvm[16];
	glGetFloatv(GL_MODELVIEW_MATRIX, mvm);
	s.add_uniform_matrid_4x4(var_name, mvm, 0);
}


void set_dlights_booleans(shader_t &s, bool enable, int shader_type) {

	if (!enable) s.set_prefix("#define NO_DYNAMIC_LIGHTS", shader_type); // if we're not even enabling dlights
	s.set_bool_prefix("has_dir_lights",  has_dir_lights, shader_type);
	s.set_bool_prefix("enable_dlights",  (enable && dl_tid > 0 && has_dl_sources), shader_type);
}


void common_shader_block_pre(shader_t &s, bool dlights, bool use_shadow_map, bool indir_lighting, float min_alpha) {

	s.set_prefix("#define USE_GOOD_SPECULAR", 1); // FS
	if (!glIsEnabled(GL_FOG)) s.set_prefix("#define NO_FOG",        1); // FS
	if (min_alpha == 0.0)     s.set_prefix("#define NO_ALPHA_TEST", 1); // FS

	for (unsigned i = 0; i < 2; ++i) {
		s.set_bool_prefix("indir_lighting", indir_lighting, i); // VS/FS
	}
	s.set_bool_prefix("use_shadow_map", use_shadow_map, 1); // FS
	set_dlights_booleans(s, dlights, 1); // FS
}


void set_indir_lighting_block(shader_t &s, bool use_smoke_indir) {

	if (use_smoke_indir && smoke_tid) {
		set_multitex(1);
		bind_3d_texture(smoke_tid);
	}
	set_multitex(0);
	s.add_uniform_int("smoke_and_indir_tex", 1);
	s.add_uniform_float("half_dxy", HALF_DXY);
	s.add_uniform_float("indir_vert_offset", indir_vert_offset);
	colorRGB const black_color(0.0, 0.0, 0.0);
	s.add_uniform_color("const_indir_color", (have_indir_smoke_tex ? black_color : const_indir_color));
}


void common_shader_block_post(shader_t &s, bool dlights, bool use_shadow_map, bool use_smoke_indir, float min_alpha) {

	s.setup_scene_bounds();
	s.setup_fog_scale(); // fog scale for the case where smoke is disabled
	if (dlights && dl_tid > 0) setup_dlight_textures(s);
	set_indir_lighting_block(s, use_smoke_indir);
	s.add_uniform_int("tex0", 0);
	s.add_uniform_float("min_alpha", min_alpha);
	if (use_shadow_map) set_smap_shader_for_all_lights(s, cobj_z_bias);
}


void set_smoke_shader_prefixes(shader_t &s, int use_texgen, bool keep_alpha, bool direct_lighting,
	bool smoke_enabled, bool has_lt_atten, bool use_smap, bool use_bmap, bool use_spec_map, bool use_mvm, bool use_tsl)
{
	s.set_int_prefix ("use_texgen",      use_texgen,      0); // VS
	s.set_bool_prefix("keep_alpha",      keep_alpha,      1); // FS
	s.set_bool_prefix("direct_lighting", direct_lighting, 1); // FS
	s.set_bool_prefix("do_lt_atten",     has_lt_atten,    1); // FS
	s.set_bool_prefix("two_sided_lighting",  use_tsl,     1); // FS
	s.set_bool_prefix("use_world_space_mvm", use_mvm,     0); // VS
	if (use_spec_map) s.set_prefix("#define USE_SPEC_MAP", 1); // FS
	
	for (unsigned i = 0; i < 2; ++i) {
		// Note: dynamic_smoke_shadows applies to light0 only
		// Note: dynamic_smoke_shadows still uses the visible smoke bbox, so if you can't see smoke it won't cast a shadow
		s.set_bool_prefix("dynamic_smoke_shadows", DYNAMIC_SMOKE_SHADOWS, i); // VS/FS
		s.set_bool_prefix("smoke_enabled",         smoke_enabled,         i); // VS/FS
		if (use_bmap) s.set_prefix("#define USE_BUMP_MAP",                i); // VS/FS
	}
	s.setup_enabled_lights(8);
}


// texture units used: 0: object texture, 1: smoke/indir lighting texture, 2-4 dynamic lighting, 5: bump map, 6-7 shadow map, 8: specular map
colorRGBA setup_smoke_shaders(shader_t &s, float min_alpha, int use_texgen, bool keep_alpha, bool indir_lighting, bool direct_lighting,
	bool dlights, bool smoke_en, bool has_lt_atten, bool use_smap, bool use_bmap, bool use_spec_map, bool use_mvm, bool force_tsl)
{
	bool const smoke_enabled(smoke_en && smoke_exists && smoke_tid > 0);
	bool const use_shadow_map(use_smap && shadow_map_enabled());
	indir_lighting &= have_indir_smoke_tex;
	smoke_en       &= have_indir_smoke_tex;
	common_shader_block_pre(s, dlights, use_shadow_map, indir_lighting, min_alpha);
	set_smoke_shader_prefixes(s, use_texgen, keep_alpha, direct_lighting, smoke_enabled, has_lt_atten, use_smap, use_bmap, use_spec_map, use_mvm, (two_sided_lighting || force_tsl));
	s.set_vert_shader("texture_gen.part+line_clip.part*+bump_map.part+indir_lighting.part+no_lt_texgen_smoke");
	s.set_frag_shader("fresnel.part*+linear_fog.part+bump_map.part+spec_map.part+ads_lighting.part*+dynamic_lighting.part*+shadow_map.part*+line_clip.part*+indir_lighting.part+textured_with_smoke");
	s.begin_shader();

	if (use_texgen == 2) {
		s.register_attrib_name("tex0_s", TEX0_S_ATTR);
		s.register_attrib_name("tex0_t", TEX0_T_ATTR);
	}
	if (use_bmap)     s.add_uniform_int("bump_map", 5);
	if (use_spec_map) s.add_uniform_int("spec_map", 8);
	common_shader_block_post(s, dlights, use_shadow_map, (smoke_en || indir_lighting), min_alpha);
	float const step_delta_scale(get_smoke_at_pos(get_camera_pos()) ? 1.0 : 2.0);
	s.add_uniform_float_array("smoke_bb", &cur_smoke_bb.d[0][0], 6);
	s.add_uniform_float("step_delta", step_delta_scale*HALF_DXY);
	if (use_mvm) upload_mvm_to_shader(s, "world_space_mvm");

	// setup fog
	//return change_fog_color(GRAY);
	colorRGBA old_fog_color;
	glGetFloatv(GL_FOG_COLOR, (float *)&old_fog_color);
	if (smoke_enabled) glFogfv(GL_FOG_COLOR, (float *)&GRAY); // for smoke
	return old_fog_color;
}


void end_smoke_shaders(shader_t &s, colorRGBA const &orig_fog_color) {

	s.end_shader();
	disable_multitex_a();
	glFogfv(GL_FOG_COLOR, (float *)&orig_fog_color); // reset to original value
}


void set_tree_branch_shader(shader_t &s, bool direct_lighting, bool dlights, bool use_smap, bool use_geom_shader) {

	unsigned const def_ndiv = 12; // default for geom shader
	bool const use_shadow_map(use_smap && shadow_map_enabled());
	common_shader_block_pre(s, dlights, use_shadow_map, 0, 0.0);
	set_smoke_shader_prefixes(s, 0, 0, direct_lighting, 0, 0, use_smap, 0, 0, 0, 0);
	s.set_vert_shader(use_geom_shader ? "tree_branches_as_lines" : "texture_gen.part+line_clip.part*+bump_map.part+indir_lighting.part+no_lt_texgen_smoke");
	s.set_frag_shader("fresnel.part*+linear_fog.part+bump_map.part+ads_lighting.part*+dynamic_lighting.part*+shadow_map.part*+line_clip.part*+indir_lighting.part+textured_with_smoke");
	
	if (use_geom_shader) {
		s.set_geom_shader("line_to_cylinder", GL_LINES, GL_TRIANGLE_STRIP, 2*(def_ndiv + 1)); // with adjacency?
	}
	s.begin_shader();
	common_shader_block_post(s, dlights, use_shadow_map, 0, 0.0);
	if (use_geom_shader) {s.add_uniform_int("ndiv", def_ndiv);}
	check_gl_error(400);
}


// texture units used: 0,8: object texture, 1: indir lighting texture, 2-4 dynamic lighting, 5: 3D noise texture, 6-7 shadow map
void setup_procedural_shaders(shader_t &s, float min_alpha, bool indir_lighting, bool dlights, bool use_smap,
	bool use_noise_tex, float tex_scale, float noise_scale, float tex_mix_saturate)
{
	bool const use_shadow_map(use_smap && shadow_map_enabled());
	indir_lighting &= have_indir_smoke_tex;
	common_shader_block_pre(s, dlights, use_shadow_map, indir_lighting, min_alpha);
	s.set_bool_prefix("use_noise_tex",  use_noise_tex,  1); // FS
	s.setup_enabled_lights(2); // only 2, but could be up to 8 later
	s.set_vert_shader("indir_lighting.part+procedural_gen");
	s.set_frag_shader("linear_fog.part+ads_lighting.part*+dynamic_lighting.part*+shadow_map.part*+triplanar_texture.part+procedural_texture.part+indir_lighting.part+procedural_gen");
	s.begin_shader();
	common_shader_block_post(s, dlights, use_shadow_map, indir_lighting, min_alpha);
	s.add_uniform_int("tex1", 8);
	s.add_uniform_float("tex_scale", tex_scale);

	if (use_noise_tex) {
		s.add_uniform_int("noise_tex", 5); // does this need an enable option?
		s.add_uniform_float("noise_scale", noise_scale);
		s.add_uniform_float("tex_mix_saturate", tex_mix_saturate);
	}
}


void setup_object_render_data() {

	RESET_TIME;
	bool const TIMETEST(0);
	calc_cur_ambient_diffuse();
	if (TIMETEST) {PRINT_TIME("Init");}
	distribute_smoke();
	upload_smoke_indir_texture();
	if (TIMETEST) {PRINT_TIME("Distribute Smoke");}
	add_dynamic_lights();
	if (TIMETEST) {PRINT_TIME("Add Dlights");}
	upload_dlights_textures();
	if (TIMETEST) {PRINT_TIME("Dlights Textures");}
	get_occluders();
	if (TIMETEST) {PRINT_TIME("Get Occluders");}
}


void end_group(int &last_group_id) {

	if (last_group_id < 0) return;
	assert((unsigned)last_group_id < obj_draw_groups.size());
	if (!obj_draw_groups[last_group_id].skip_render()) glEnd();
	obj_draw_groups[last_group_id].end_render();
	if (group_back_face_cull) glDisable(GL_CULL_FACE);
	last_group_id = -1;
}


// should always have draw_solid enabled on the first call for each frame
void draw_coll_surfaces(bool draw_solid, bool draw_trans) {

	RESET_TIME;
	assert(draw_solid || draw_trans);
	static vector<pair<float, int> > draw_last;
	if (coll_objects.empty() || coll_objects.drawn_ids.empty() || world_mode != WMODE_GROUND) return;
	if (!draw_solid && draw_last.empty() && (!smoke_exists || portals.empty())) return; // nothing transparent to draw
	set_lighted_sides(2);
	set_fill_mode();
	gluQuadricTexture(quadric, GL_FALSE);
	glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
	glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
	glEnable(GL_TEXTURE_GEN_S);
	glEnable(GL_TEXTURE_GEN_T);
	glDisable(GL_LIGHTING); // custom lighting calculations from this point on
	set_color_a(BLACK);
	set_specular(0.0, 1.0);
	bool has_lt_atten(draw_trans && !draw_solid && coll_objects.has_lt_atten);
	// Note: enable direct_lighting if processing sun/moon shadows here
	shader_t s;
	colorRGBA const orig_fog_color(setup_smoke_shaders(s, 0.0, 2, 0, 1, 1, 1, 1, has_lt_atten, 1));
	if (!s.is_setup()) has_lt_atten = 0; // shaders disabled
	int last_tid(-1), last_group_id(-1), last_type(-1);
	
	if (draw_solid) {
		draw_last.resize(0);

		for (cobj_id_set_t::const_iterator i = coll_objects.drawn_ids.begin(); i != coll_objects.drawn_ids.end(); ++i) {
			unsigned cix(*i);
			assert(cix < coll_objects.size());
			coll_obj const &c(coll_objects[cix]);
			assert(c.cp.draw);
			if (c.no_draw()) continue; // can still get here sometimes
				
			if (c.is_semi_trans()) { // slow when polygons are grouped
				float dist(distance_to_camera(c.get_center_pt()));

				if (c.type == COLL_SPHERE) { // distance to surface closest to the camera
					dist -= c.radius;
				}
				else if (c.type == COLL_CYLINDER || c.type == COLL_CYLINDER_ROT) { // approx distance to surface closest to the camera
					dist -= min(0.5*(c.radius + c.radius2), 0.5*p2p_dist(c.points[0], c.points[1]));
				}
				draw_last.push_back(make_pair(-dist, cix)); // negative distance
			}
			else {
				if (c.type != last_type && c.type == COLL_SPHERE) {
					glFlush(); // HACK: need a flush before texture matrix updates for spheres - FIXME: better way to do this?
				}
				last_type = c.type;
				c.draw_cobj(cix, last_tid, last_group_id, &s); // i may not be valid after this call
				
				if (cix != *i) {
					assert(cix > *i);
					i = std::lower_bound(i, coll_objects.drawn_ids.end(), cix);
				}
			}
		} // for i
		end_group(last_group_id);
	} // end draw solid
	if (draw_trans) { // called second
		if (smoke_exists) {
			for (unsigned i = 0; i < portals.size(); ++i) {
				if (!portals[i].is_visible()) continue;
				float const neg_dist_sq(-distance_to_camera_sq(portals[i].get_center_pt()));
				draw_last.push_back(make_pair(neg_dist_sq, -(int)(i+1)));
			}
		}
		sort(draw_last.begin(), draw_last.end()); // sort back to front
		enable_blend();
		int ulocs[3] = {0};
		float last_light_atten(-1.0), last_refract_ix(0.0); // set to invalid values to start

		if (has_lt_atten) {
			ulocs[0] = s.get_uniform_loc("light_atten");
			ulocs[1] = s.get_uniform_loc("cube_bb"    );
			ulocs[2] = s.get_uniform_loc("refract_ix" );
			assert(ulocs[0] && ulocs[1] && ulocs[2]);
		}
		for (unsigned i = 0; i < draw_last.size(); ++i) {
			int const ix(draw_last[i].second);

			if (ix < 0) { // portal
				end_group(last_group_id);

				if (has_lt_atten && last_light_atten != 0.0) {
					s.set_uniform_float(ulocs[0], 0.0);
					last_light_atten = 0.0;
				}
				if (has_lt_atten && last_refract_ix != 1.0) {
					s.set_uniform_float(ulocs[2], 1.0);
					last_refract_ix = 1.0;
				}
				unsigned const pix(-(ix+1));
				assert(pix < portals.size());
				portals[pix].draw();
			}
			else { // cobj
				unsigned cix(ix);
				assert(cix < coll_objects.size());
				coll_obj const &c(coll_objects[cix]);
				
				if (has_lt_atten) { // we only support cubes for now (Note: may not be compatible with groups)
					float const light_atten((c.type == COLL_CUBE) ? c.cp.light_atten : 0.0);

					if (light_atten != last_light_atten) {
						s.set_uniform_float(ulocs[0], light_atten);
						last_light_atten = light_atten;
					}
					if (c.cp.refract_ix != last_refract_ix) {
						s.set_uniform_float(ulocs[2], c.cp.refract_ix);
						last_refract_ix = c.cp.refract_ix;
					}
					if (light_atten > 0.0) s.set_uniform_float_array(ulocs[1], (float const *)c.d, 6);
				}
				c.draw_cobj(cix, last_tid, last_group_id, &s);
				assert(cix == ix); // should not have changed
			}
		} // for i
		end_group(last_group_id);
		disable_blend();
		draw_last.resize(0);
	} // end draw_trans
	end_smoke_shaders(s, orig_fog_color);
	glEnable(GL_LIGHTING);
	disable_textures_texgen();
	set_lighted_sides(1);
	set_specular(0.0, 1.0);
	//if (draw_solid) PRINT_TIME("Final Draw");
}


bool portal::is_visible() const {

	point center;
	float rad;
	polygon_bounding_sphere(pts, 4, 0.0, center, rad);
	return sphere_in_camera_view(center, rad, 2);
}


void portal::draw() const {

	float const scale[2] = {0.0, 0.0}, xlate[2] = {0.0, 0.0};
	select_texture(WHITE_TEX, 0);
	setup_polygon_texgen(plus_z, scale, xlate, zero_vector); // doesn't matter as long as it's set to something
	ALPHA0.do_glColor();
	//WHITE.do_glColor();
	glBegin(GL_QUADS);

	for (unsigned i = 0; i < 4; ++i) {
		pts[i].do_glVertex();
	}
	glEnd();
};


void draw_stars(float alpha) {

	assert(num_stars <= stars.size());
	if (alpha <= 0.0) return;
	colorRGBA color(BLACK), bkg;
	UNROLL_3X(bkg[i_] = (1.0 - alpha)*bkg_color[i_];)
	glPushMatrix();
	if (camera_mode == 1) translate_to(surface_pos);
	up_norm.do_glNormal();
	set_color(BLACK);
	enable_blend();
	glPointSize(2.0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glBegin(GL_POINTS);
	
	for (unsigned i = 0; i < num_stars; ++i) {
		if ((rand()%400) == 0) continue; // flicker out

		for (unsigned j = 0; j < 3; ++j) {
			float const c(stars[i].color[j]*stars[i].intensity);
			color[j] = ((alpha >= 1.0) ? c : (alpha*c + bkg[j]));
		}
		color.do_glColor();
		stars[i].pos.do_glVertex();
	}
	glEnd();
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);
	glPointSize(1.0);
	disable_blend();
	glPopMatrix();
}


void draw_sun() {

	if (!have_sun) return;
	point const pos(get_sun_pos());

	if (sphere_in_camera_view(pos, sun_radius, 1)) {
		//select_texture(SUN_TEX);
		glDisable(GL_LIGHTING);
		colorRGBA color(SUN_C);
		apply_red_sky(color);
		color.do_glColor();
#if 0
		unsigned occ_query(0);
		glGenQueries(1, &occ_query);
		glBeginQuery(GL_SAMPLES_PASSED, occ_query);
#endif
		draw_subdiv_sphere(pos, sun_radius, N_SPHERE_DIV, 1, 0);
		glEnable(GL_LIGHTING);
		//glDisable(GL_TEXTURE_2D);
#if 0
		glEndQuery(GL_SAMPLES_PASSED);
		unsigned pixel_count(0);
		glGetQueryObjectuiv(occ_query, GL_QUERY_RESULT, &pixel_count);
		glDeleteQueries(1, &occ_query);
		cout << "pixel count: " << pixel_count << endl;
#endif
	}
}


void draw_moon() {

	if (show_fog) return; // don't draw when there is fog
	point const pos(get_moon_pos());
	if (!sphere_in_camera_view(pos, moon_radius, 1)) return;
	set_color(WHITE);
	glDisable(GL_LIGHT0);
	glDisable(GL_LIGHT1);
	float const ambient[4] = {0.05, 0.05, 0.05, 1.0}, diffuse[4] = {1.0, 1.0, 1.0, 1.0};

	if (have_sun) {
		set_gl_light_pos(GL_LIGHT4, get_sun_pos(), 0.0);
		set_colors_and_enable_light(GL_LIGHT4, ambient, diffuse);
	}
	select_texture(MOON_TEX);
	draw_subdiv_sphere(pos, moon_radius, N_SPHERE_DIV, 1, 0);
	glDisable(GL_TEXTURE_2D);
	if (light_factor < 0.6) glEnable(GL_LIGHT1); // moon
	if (light_factor > 0.4) glEnable(GL_LIGHT0); // sun
	glDisable(GL_LIGHT4);

	if (light_factor >= 0.4) { // fade moon into background color when the sun comes up
		colorRGBA color = bkg_color;
		color.alpha     = 5.0*(light_factor - 0.4);
		glDisable(GL_LIGHTING);
		enable_blend();
		color.do_glColor();
		draw_subdiv_sphere(pos, 1.2*moon_radius, N_SPHERE_DIV, 0, 0);
		glEnable(GL_LIGHTING);
		disable_blend();
	}
}


// for some reason the texture is backwards, so we mirrored the image of the earth
void draw_earth() {

	if (show_fog) return; // don't draw when there is fog
	point pos(mesh_origin + earth_pos);
	if (camera_mode == 1) pos += surface_pos;
	static float rot_angle(0.0);

	if (quadric != 0 && sphere_in_camera_view(pos, earth_radius, 1)) {
		set_fill_mode();
		select_texture(EARTH_TEX);
		set_color(WHITE);
		glPushMatrix();
		translate_to(pos);
		glRotatef(67.0, 0.6, 0.8, 0.0);
		glRotatef(rot_angle, 0.0, 0.0, 1.0);
		glRotatef(180.0, 1.0, 0.0, 0.0);
		draw_sphere_dlist(all_zeros, earth_radius, N_SPHERE_DIV, 1);
		glPopMatrix();
		glDisable(GL_TEXTURE_2D);
	}
	rot_angle += 0.2*fticks;
}


void draw_stationary_earth(float radius) {

	set_fill_mode();
	select_texture(EARTH_TEX);
	set_color(WHITE);
	draw_subdiv_sphere(all_zeros, radius, N_SPHERE_DIV, 1, 0);
	glDisable(GL_TEXTURE_2D);
}


void apply_red_sky(colorRGBA &color) {

	if (light_factor > 0.45 && light_factor < 0.55) { // red sky at night/morning
		float const redness(1.0 - 20.0*fabs(light_factor - 0.5));
		color.R = min(1.0f, (1.0f + 0.8f*redness)*color.R);
		color.G = max(0.0f, (1.0f - 0.2f*redness)*color.G);
		color.B = max(0.0f, (1.0f - 0.5f*redness)*color.B);
	}
}


colorRGBA get_cloud_color() {

	colorRGBA color(brightness, brightness, brightness, atmosphere);
	apply_red_sky(color);
	return color;
}


float get_cloud_density(point const &pt, vector3d const &dir) { // optimize?

	if (atmosphere == 0.0) return 0.0;
	point lsint;
	if (!line_sphere_int(dir*-1.0, pt, cur_spo.center, cur_spo.radius, lsint, 0)) return 0.0; // shouldn't get here?
	vector3d const vdir(lsint - cur_spo.center);
	return atmosphere*get_texture_component(CLOUD_TEX, (vdir.x*cur_spo.radius_inv + cur_spo.dx), (vdir.y*cur_spo.radius_inv + cur_spo.dy), 3); // cloud alpha
}


void draw_puffy_clouds(int order) {

	if (cloud_manager.is_inited() && (get_camera_pos().z > cloud_manager.get_z_plane()) != order) return;

	if (atmosphere < 0.01) {
		cloud_manager.clear();
	}
	else if (display_mode & 0x40) { // key 7
		cloud_manager.draw();
	}
}


void draw_sky(int order) {

	if (atmosphere < 0.01) return; // no atmosphere
	set_specular(0.0, 1.0);
	float radius(0.55*(FAR_CLIP+X_SCENE_SIZE));
	point center((camera_mode == 1) ? surface_pos : mesh_origin);
	center.z -= 0.727*radius;
	if ((distance_to_camera(center) > radius) != order) return;
	colorRGBA const cloud_color(get_cloud_color());

	static float sky_rot_xy[2] = {0.0, 0.0}; // x, y
	float const wmag(sqrt(wind.x*wind.x + wind.y*wind.y));

	if (wmag > TOLERANCE) {
		for (unsigned d = 0; d < 2; ++d) {
			sky_rot_xy[d] += fticks*CLOUD_WIND_SPEED*(wmag + 0.5*WIND_ADJUST)*wind[d]/wmag;
		}
	}
	cur_spo = sky_pos_orient(center, radius, sky_rot_xy[0], sky_rot_xy[1]);
	int const light(GL_LIGHT4);
	set_fill_mode();
	enable_blend();

	if (have_sun && light_factor > 0.4) { // sun lighting of clouds
		float diffuse[4], ambient[4];
		point lpos(get_sun_pos()), lsint;
		vector3d const sun_v((get_camera_pos() - lpos).get_norm());
		if (line_sphere_int(sun_v, lpos, center, radius, lsint, 1)) lpos = lsint;
		
		for (unsigned i = 0; i < 4; ++i) { // even alpha?
			diffuse[i] = 1.0*sun_color[i];
			ambient[i] = 0.5*sun_color[i];
		}
		set_gl_light_pos(light, lpos, 1.0); // w - point light source
		set_colors_and_enable_light(light, ambient, diffuse);
		glLightf(light, GL_CONSTANT_ATTENUATION,  0.0);
		glLightf(light, GL_LINEAR_ATTENUATION,    0.01);
		glLightf(light, GL_QUADRATIC_ATTENUATION, 0.01);
	}
	if (have_sun && light_factor > 0.4) { // draw horizon
		glDisable(GL_LIGHTING);
		colorRGBA horizon_color;
		float const blend_val(atmosphere*CLIP_TO_01(10.0f*(light_factor - 0.4f)));
		blend_color(horizon_color, WHITE, ALPHA0, blend_val, 1);
		horizon_color.alpha *= 0.5;
		apply_red_sky(horizon_color);
		horizon_color.do_glColor();
		select_texture(GRADIENT_TEX);
		draw_sphere_dlist(center, 1.05*radius, N_SPHERE_DIV, 1);
		glEnable(GL_LIGHTING);
	}
	select_texture(CLOUD_TEX);

	// change S and T parameters to map sky texture into the x/y plane with translation based on wind/rot
	setup_texgen(1.0/radius, 1.0/radius, (sky_rot_xy[0] - center.x/radius), (sky_rot_xy[1] - center.y/radius)); // GL_EYE_LINEAR
	set_color_a(cloud_color);
	set_color_d(cloud_color); // disable lighting (BLACK)?
	//draw_sphere_at(center, radius, (3*N_SPHERE_DIV)/2);
	draw_subdiv_sphere(center, radius, (3*N_SPHERE_DIV)/2, zero_vector, NULL, 0, 1);
	disable_textures_texgen(); // reset S and T parameters
	disable_blend();
	glDisable(light);
}


void draw_stationary_sky(float radius, float density) {

	colorRGBA color(WHITE);
	color.alpha = density;
	set_fill_mode();
	enable_blend();
	select_texture(CLOUD_TEX);
	set_color(color);
	draw_subdiv_sphere(all_zeros, radius, N_SPHERE_DIV, 1, 0);
	glDisable(GL_TEXTURE_2D);
	disable_blend();
}


void compute_brightness() {

	brightness = 0.8 + 0.2*light_factor;
	if (!have_sun) brightness *= 0.25;
	if (is_cloudy) brightness *= 0.5;
	
	if (light_pos.z < zmin) {
		brightness *= 0.1;
	}
	else if (light_factor <= 0.4 || light_factor >= 0.6) {
		brightness *= 0.15 + 0.85*light_pos.z/light_pos.mag();
	}
	else {
		float const sun_bright (sun_pos.z /sun_pos.mag() );
		float const moon_bright(moon_pos.z/moon_pos.mag());
		brightness *= 0.15 + 0.85*5.0*((light_factor - 0.4)*sun_bright + (0.6 - light_factor)*moon_bright);
	}
	brightness = max(0.99f, min(0.0f, brightness));
}


template<typename T> void get_draw_order(vector<T> const &objs, order_vect_t &order) {

	point const camera(get_camera_pos());
	
	for (unsigned i = 0; i < objs.size(); ++i) {
		if (!objs[i].status) continue;
		point const pos(objs[i].get_pos());

		if (sphere_in_camera_view(pos, objs[i].radius, 0)) {
			order.push_back(make_pair(-p2p_dist_sq(pos, camera), i));
		}
	}
	sort(order.begin(), order.end()); // sort back to front
}


void bubble::draw() const {

	assert(status);
	colorRGBA color2(color);
	if (world_mode == WMODE_GROUND) select_liquid_color(color2, pos);
	float const point_dia(NDIV_SCALE*window_width*radius/distance_to_camera(pos));

	if (point_dia < 4.0) {
		bubble_pld.add_pt(pos, (get_camera_pos() - pos), color2);
	}
	else {
		set_color(color2);
		int const ndiv(max(4, min(16, int(4.0*sqrt(point_dia)))));
		draw_sphere_dlist(pos, radius, ndiv, 0, 0);
	}
}


order_vect_t particle_cloud::order;


void particle_cloud::draw() const {

	assert(status);
	float const scale(get_zoom_scale()*0.016*window_width);
	colorRGBA color(base_color);

	if (is_fire()) {
		color.G *= get_rscale();
	}
	else {
		color *= (0.5*(1.0 - darkness));
	}
	color.A *= density;
	float const dist(distance_to_camera(pos));
	int const ndiv(max(4, min(16, int(scale/dist))));

	if (parts.empty()) {
		if (status && sphere_in_camera_view(pos, radius, 0)) {
			draw_part(pos, radius, color);
		}
	}
	else {
		order.resize(0);
		vector<part> cur_parts(parts);

		for (unsigned i = 0; i < cur_parts.size(); ++i) {
			cur_parts[i].pos     = pos + cur_parts[i].pos*radius;
			cur_parts[i].radius *= radius;
		}
		get_draw_order(cur_parts, order);
		
		for (unsigned j = 0; j < order.size(); ++j) {
			unsigned const i(order[j].second);
			assert(i < cur_parts.size());
			draw_part(cur_parts[i].pos, cur_parts[i].radius, color);
		}
	}
}


void particle_cloud::draw_part(point const &p, float r, colorRGBA c) const {

	point const camera(get_camera_pos());
	if (dist_less_than(camera, p, max(NEAR_CLIP, 4.0f*r))) return; // too close to the camera

	if (!no_lighting && !is_fire()) { // fire has its own emissive lighting
		int cindex;
		float rad, dist, t;
		point const lpos(get_light_pos());
	
		if (!check_coll_line(p, lpos, cindex, -1, 1, 1)) { // not shadowed (slow, especially for lots of smoke near trees)
			// Note: This can be moved into a shader, but the performance and quality improvement might not be significant
			vector3d const dir((p - get_camera_pos()).get_norm());
			float const dp(dot_product_ptv(dir, p, lpos));
			blend_color(c, WHITE, c, 0.15, 0); // 15% ambient lighting (transmitted/scattered)
			if (dp > 0.0) blend_color(c, WHITE, c, 0.1*dp/p2p_dist(p, lpos), 0); // 10% diffuse lighting (directional)

			if (dp < 0.0 && have_sun && line_intersect_sphere(p, dir, sun_pos, 6*sun_radius, rad, dist, t)) {
				float const mult(1.0 - max(0.0f, (rad - sun_radius)/(5*sun_radius)));
				blend_color(c, SUN_C, c, 0.75*mult, 0); // 75% direct sun lighting
			}
		}
		get_indir_light(c, p, 0, 1, NULL, NULL); // could move outside of the parts loop if too slow
	}
	if (red_only) c.G = c.B = 0.0; // for special luminosity cloud texture rendering
	c.do_glColor();
	// Note: Can disable smoke volume integration for close smoke, but very close smoke (< 1 grid unit) is infrequent
	draw_billboard(p, camera, up_vector, 4.0*r, 4.0*r);
}


void fire::set_fire_color() const {

	float const alpha(rand_uniform(max(0.3, (0.9 + 0.1*heat)), min(0.9, (0.8 + 0.2*heat))));
	colorRGBA const color(1.0, 0.4*heat, max(0.0f, 1.2f*(heat-1.0f)), alpha);
	color.do_glColor();
}


void fire::draw() const {

	assert(status);
	point const pos2(pos + point(0.0, 0.0, 2.0*radius));
	WHITE.do_glColor();
	draw_animated_billboard(pos2, 4.0*radius, (time&15)/16.0);
}


void decal_obj::draw() const {

	assert(status);
	colorRGBA draw_color(color);
	point const cur_pos(get_pos());

	if (color != BLACK) {
		bool is_shadowed(pt_is_shadowed(cur_pos, get_light(), radius, -1, 0, 0));
		colorRGBA const d(is_shadowed ? BLACK : draw_color);
		colorRGBA a(draw_color);
		get_shadowed_color(a, cur_pos, is_shadowed, 0, 0);
		blend_color(draw_color, a, d, 0.5, 0);
		draw_color.set_valid_color();
	}
	draw_color.alpha = get_alpha();
	draw_color.do_glColor();
	vector3d const upv(orient.y, orient.z, orient.x); // swap the xyz values to get an orthogonal vector
	draw_billboard(cur_pos, (cur_pos + orient), upv, radius, radius);
}


template<typename T> void draw_objects(vector<T> const &objs) {

	order_vect_t order;
	get_draw_order(objs, order);

	for (unsigned i = 0; i < order.size(); ++i) {
		assert(order[i].second < objs.size());
		objs[order[i].second].draw();
	}
}


void draw_bubbles() {

	if (bubbles.empty()) return;
	glEnable(GL_CULL_FACE);
	enable_blend();
	set_color(WATER_C);
	draw_objects(bubbles);
	bubble_pld.draw_and_clear();
	disable_blend();
	glDisable(GL_CULL_FACE);
}


void draw_part_cloud(vector<particle_cloud> const &pc, colorRGBA const color, bool zoomed) {

	enable_flares(color, zoomed); // color will be set per object
	//select_multitex(CLOUD_TEX, 1);
	glAlphaFunc(GL_GREATER, 0.01);
	glEnable(GL_ALPHA_TEST); // makes it faster
	glBegin(GL_QUADS);
	draw_objects(pc);
	glEnd();
	glDisable(GL_ALPHA_TEST);
	disable_flares();
	//disable_multitex_a();
}


void draw_smoke() {

	if (part_clouds.empty()) return; // Note: just because part_clouds is empty doesn't mean there is any enabled smoke
	set_color(BLACK);
	shader_t s;
	colorRGBA const orig_fog_color(setup_smoke_shaders(s, 0.01, 0, 1, 0, 0, 0, 1)); // slow when a lot of smoke is up close
	draw_part_cloud(part_clouds, WHITE, 0);
	end_smoke_shaders(s, orig_fog_color);
}


template<typename T> void draw_billboarded_objs(obj_vector_t<T> const &objs, int tid) {

	order_vect_t order;
	get_draw_order(objs, order);
	if (order.empty()) return;
	shader_t s;
	colorRGBA const orig_fog_color(setup_smoke_shaders(s, 0.04, 0, 1, 0, 0, 0, 1));
	enable_blend();
	set_color(BLACK);
	glDisable(GL_LIGHTING);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.04);
	select_texture(tid);
	glBegin(GL_QUADS);

	for (unsigned j = 0; j < order.size(); ++j) {
		unsigned const i(order[j].second);
		assert(i < objs.size());
		objs[i].draw();
	}
	glEnd();
	end_smoke_shaders(s, orig_fog_color);
	glDisable(GL_ALPHA_TEST);
	disable_blend();
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_LIGHTING);
}


void draw_fires() {

	// animated fire textured quad
	//glDisable(GL_DEPTH_TEST);
	draw_billboarded_objs(fires, FIRE_TEX);
}


struct crack_point {

	point pos, orig_pos;
	int cid, face, time;
	float alpha;
	colorRGBA color;
	
	crack_point() {}
	crack_point(point const &pos_, point const &opos, int cid_, int face_, int time_, float alpha_, colorRGBA const &color_)
		: pos(pos_), orig_pos(opos), cid(cid_), face(face_), time(time_), alpha(alpha_), color(color_) {}
	
	bool operator<(crack_point const &c) const {
		if (cid  != c.cid ) return (cid  < c.cid );
		if (face != c.face) return (face < c.face);
		return (c.time < time); // max time first
	}
};


struct ray2d {

	point2d<float> pts[2];

	ray2d() {}
	ray2d(float x1, float y1, float x2, float y2) {pts[0].x = x1; pts[0].y = y1; pts[1].x = x2; pts[1].y = y2;}
};


void create_and_draw_cracks() {

	vector<crack_point> cpts;  // static?
	vector<ray2d> crack_lines; // static?
	int last_cobj(-1);
	bool skip_cobj(0);
	point const camera(get_camera_pos());

	for (vector<decal_obj>::const_iterator i = decals.begin(); i != decals.end(); ++i) {
		if (i->status == 0 || !i->is_glass || i->cid < 0) continue;
		if (i->cid == last_cobj && skip_cobj)             continue;
		point const pos(i->get_pos());
		if (!dist_less_than(camera, pos, 1000*i->radius)) continue; // too far away
		assert((unsigned)i->cid < coll_objects.size());
		coll_obj const &cobj(coll_objects[i->cid]);
		skip_cobj = (cobj.status != COLL_STATIC || cobj.type != COLL_CUBE || !camera_pdu.cube_visible(cobj) || cobj.is_occluded_from_camera());
		last_cobj = i->cid;
		if (skip_cobj) continue;
		int const face(cobj.closest_face(pos)), dim(face >> 1), dir(face & 1);
		if ((pos[dim] - camera[dim] < 0) ^ dir) continue; // back facing
		cpts.push_back(crack_point(pos, i->pos, i->cid, face, i->time, i->get_alpha(), i->color));
	}
	stable_sort(cpts.begin(), cpts.end());

	for (unsigned i = 0; i < cpts.size();) {
		unsigned const s(i);
		for (++i; i < cpts.size() && cpts[i].cid == cpts[s].cid && cpts[i].face == cpts[s].face; ++i) {}
		// all cpts in [s,i) have the same {cid, face}
		crack_lines.resize(0);
		cube_t const &cube(coll_objects[cpts[s].cid]);
		float const diameter(cube.get_bsphere_radius());
		
		for (unsigned j = s; j < i; ++j) { // generated cracks to the edge of the glass cube
			crack_point const &cpt1(cpts[j]);
			int const dim(cpt1.face >> 1), d1((dim+1)%3), d2((dim+2)%3);
			unsigned const ncracks(4); // one for each quadrant
			float const center(0.5*(cube.d[dim][0] + cube.d[dim][1]));
			float const x1(cpt1.pos[d1]), y1(cpt1.pos[d2]);
			rand_gen_t rgen;
			rgen.set_state(*(int *)&cpt1.orig_pos[d1], *(int *)&cpt1.orig_pos[d2]); // hash floats as ints	
			point epts[ncracks];

			for (unsigned n = 0; n < ncracks; ++n) {
				point epos;
				float min_dist_sq(0.0);

				for (unsigned attempt = 0; attempt < 4; ++attempt) {
					vector3d dir;
					dir[dim] = 0.0;
					dir[d1]  = rgen.rand_float()*((n&1) ? -1.0 : 1.0);
					dir[d2]  = rgen.rand_float()*((n&2) ? -1.0 : 1.0);
					point p1(cpt1.pos);
					p1[dim]  = center;
					point p2(p1 + dir.get_norm()*diameter);
					if (!do_line_clip(p1, p2, cube.d)) continue; // should never fail, and p1 should never change
					p2[dim]  = cpt1.pos[dim];

					for (vector<ray2d>::const_iterator c = crack_lines.begin(); c != crack_lines.end(); ++c) {
						float const x2(p2[d1]), x3(c->pts[0].x), x4(c->pts[1].x);
						if (max(x3, x4) < min(x1, x2) || max(x1, x2) < min(x3, x4)) continue;
						float const y2(p2[d2]), y3(c->pts[0].y), y4(c->pts[1].y);
						if (max(y3, y4) < min(y1, y2) || max(y1, y2) < min(y3, y4)) continue;
						float const denom((y4 - y3)*(x2 - x1) - (x4 - x3)*(y2 - y1));
						if (fabs(denom) < TOLERANCE) continue;
						float const ub(((x2 - x1)*(y1 - y3) - (y2 - y1)*(x1 - x3))/denom);
						if (ub < 0.0 || ub > 1.0)    continue;
						float const ua(((x4 - x3)*(y1 - y3) - (y4 - y3)*(x1 - x3))/denom);
						if (ua < 0.0 || ua > 1.0)    continue;
						p2 = cpt1.pos + (p2 - cpt1.pos)*ua; // update intersection point
						if (attempt > 0 && p2p_dist_sq(cpt1.pos, p2) >= min_dist_sq) break;
					}
					float const dist_sq(p2p_dist_sq(cpt1.pos, p2));

					if (attempt == 0 || dist_sq < min_dist_sq) {
						epos = p2;
						min_dist_sq = dist_sq;
					}
				} // for attempt
				beams.push_back(beam3d(0, NO_SOURCE, cpt1.pos, epos, cpt1.color, 0.05*cpt1.alpha));
				epts[n] = epos;
			} // for n
			for (unsigned n = 0; n < ncracks; ++n) {
				crack_lines.push_back(ray2d(x1, y1, epts[n][d1], epts[n][d2]));
			}
		} // for j
	} // for i
}


void draw_decals() {

	//RESET_TIME;
	create_and_draw_cracks();
	//PRINT_TIME("Draw Cracks");
	draw_billboarded_objs(decals, BLUR_CENT_TEX);
}


void add_camera_filter(colorRGBA const &color, unsigned time, int tid, unsigned ix) {

	assert(ix < MAX_CFILTERS);
	if (color.alpha == 0.0) return;
	if (cfilters.size() <= ix) cfilters.resize(ix+1);
	cfilters[ix] = camera_filter(color, time, tid);
}


void camera_filter::draw() {

	bool const tex(tid >= 0 && glIsTexture(tid));
	if (tex) select_texture(tid);
	glBegin(GL_QUADS);
	float const zval(-1.1*perspective_nclip), tan_val(tan(perspective_fovy/TO_DEG));
	float const y(0.5*zval*tan_val), x((y*window_width)/window_height);
	color.do_glColor();
	draw_one_tquad(-x, -y, x, y, zval, tex);
	glEnd();
	if (tex) glDisable(GL_TEXTURE_2D);
}


void draw_camera_filters(vector<camera_filter> &cfs) {

	if (cfs.empty()) return;
	GLboolean lighting(glIsEnabled(GL_LIGHTING));
	if (lighting) glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	enable_blend();

	for (int i = (int)cfs.size()-1; i >= 0; --i) { // apply backwards
		if (cfs[i].time == 0) continue;
		cfs[i].draw();
		if ((int)cfs[i].time <= iticks) cfs[i].time = 0; else cfs[i].time -= iticks;
	}
	disable_blend();
	glEnable(GL_DEPTH_TEST);
	if (lighting) glEnable(GL_LIGHTING);
}


float const spark_t::radius = 0.0;


void spark_t::draw() const {

	c.do_glColor();
	point const camera(get_camera_pos());
	draw_billboard((pos + (camera - pos).get_norm()*0.02), camera, up_vector, s, s);
}


void draw_sparks() {

	if (sparks.empty()) return;
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_LIGHTING);
	enable_blend();
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.01);
	select_texture(BLUR_TEX);
	glBegin(GL_QUADS);
	draw_objects(sparks);
	glEnd();
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_LIGHTING);
	glDisable(GL_ALPHA_TEST);
	disable_blend();
	set_fill_mode();
	sparks.clear();
}


void draw_projectile_effects() {

	update_blasts(); // not really an update, but needed for draw_blasts
	draw_blasts();
	draw_beams();
	draw_sparks();
}


void draw_env_other() {

	if (!enable_fsource) return;
	set_color(BLACK);
	draw_subdiv_sphere(flow_source, 0.05, N_SPHERE_DIV, 0, 0);
}


void mouse_draw_on_ground(int x, int y) {

	swap(x, y); // landscape is rotated by 90 degrees
	int const xscale(window_height), yscale(window_height);
	int const xpos(int((float(x - 0.5*(window_width-window_height))/(float)xscale)*MESH_X_SIZE));
	int const ypos(int(((float)y/(float)yscale)*MESH_Y_SIZE));
	if (point_outside_mesh(xpos, ypos)) return;
	accumulation_matrix[ypos][xpos] += 1000.0;
	add_color_to_landscape_texture(WHITE, get_xval(xpos), get_yval(ypos), 1.0, 0);
}


void draw_splash(float x, float y, float z, float size, colorRGBA color) {

	assert(quadric && size >= 0.0);
	if (size == 0.0 || temperature <= W_FREEZE_POINT && !island) return;
	if (size > 0.1) size = sqrt(10.0*size)/10.0;
	unsigned const num_rings(min(10U, (unsigned)ceil(size)));
	size = min(size, 0.025f);
	float radius(size);
	float const dr(0.5*size);
	point const pos(x, y, z+SMALL_NUMBER);
	unsigned const ndiv(max(3, min(N_CYL_SIDES, int(1000.0*size/max(TOLERANCE, distance_to_camera(pos))))));
	select_liquid_color(color, get_xpos(x), get_ypos(y));
	set_color(color);
	set_fill_mode();
	glPushMatrix();
	translate_to(pos);

	for (unsigned i = 0; i < num_rings; ++i) {
		gluDisk(quadric, (radius - 0.5*dr), radius, ndiv, 1);
		radius += dr;
	}
	glPopMatrix();
}


void draw_text(float x, float y, float z, char const *text, float tsize, bool bitmap_font) {

	//bitmap_font |= ((display_mode & 0x80) != 0);
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);

	if (bitmap_font) {
		glRasterPos3f(x, y, z);
	}
	else {
		up_norm.do_glNormal();
		glEnable(GL_BLEND);
		glEnable(GL_LINE_SMOOTH);
		glPushMatrix();
		glTranslatef(x, y, z);
		uniform_scale(0.000005*tsize);
	}
	unsigned line_num(0);

	while (*text) {
		if (*text == '\n') { // newline (CR/LF)
			++line_num;

			if (bitmap_font) {
				glRasterPos3f(x, y-(0.5*line_num)/window_height, z);
			}
			else {
				glPopMatrix();
				glPushMatrix();
				glTranslatef(x, y-0.001*line_num*tsize, z);
				uniform_scale(0.000005*tsize);
			}
		}
		else {
			if (bitmap_font) {
				glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *text); // other fonts available
			}
			else {
				glutStrokeCharacter(GLUT_STROKE_ROMAN, *text); // GLUT_STROKE_MONO_ROMAN
			}
		}
		text++;
	}
	if (!bitmap_font) {
		glPopMatrix();
		glDisable(GL_LINE_SMOOTH);
		glDisable(GL_BLEND);
	}
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);
}


void draw_framerate(float val) {

	char text[32];
	WHITE.do_glColor();
	sprintf(text, "%3.1f", val);
	float const ar(((float)window_width)/((float)window_height));
	draw_text(-0.011*ar, -0.011, -2.0*NEAR_CLIP, text);
}


void draw_compass_and_alt() { // and temperature

	char text[64];
	float const aspect_ratio((float)window_width/(float)window_height);
	string const dirs[8] = {"N", "NW", "W", "SW", "S", "SE", "E", "NE"};
	YELLOW.do_glColor();
	sprintf(text, "Loc: (%3.2f, %3.2f, %3.2f)", (camera_origin.x+xoff2*DX_VAL), (camera_origin.y+yoff2*DY_VAL), camera_origin.z);
	draw_text(-0.005*aspect_ratio, -0.01, -0.02, text);
	float const theta(safe_acosf(-cview_dir.x)*TO_DEG);
	int const octant(int(((cview_dir.y > 0) ? (360.0 - theta) : theta)/45.0 + 2.5)%8);
	sprintf(text, "%s", dirs[octant].c_str());
	draw_text(0.005*aspect_ratio, -0.01, -0.02, text);
	sprintf(text, "Temp: %iC", int(temperature));
	draw_text(0.007*aspect_ratio, -0.01, -0.02, text);
}



