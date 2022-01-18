// 3D World - City and Building Parameter Parsing
// by Frank Gennari
// 09/04/20

#include "city.h"
#include "buildings.h"
#include "file_utils.h"


extern building_params_t global_building_params;


string const model_opt_names[NUM_OBJ_MODELS] =
{"toilet_model", "sink_model", "tub_model", "fridge_model", "stove_model", "tv_model", ""/*monitor*/, "couch_model", "office_chair_model", "urinal_model",
"lamp_model", "washer_model", "dryer_model", "key_model", "hanger_model", "clothing_model", "fire_escape_model", "cup_model", "rat_model",
"fire_hydrant_model", "substation_model", "umbrella_model"};

bool city_params_t::read_option(FILE *fp) {

	char strc[MAX_CHARS] = {0};
	if (!read_str(fp, strc)) return 0;
	string const str(strc);
	int error(0);
	// TODO: make these members of city_params_t to avoid recreating them for every option
	kw_to_val_map_t<bool     > kwmb(error, "city");
	kw_to_val_map_t<unsigned > kwmu(error, "city");
	kwmu.add("num_cities",     num_cities);
	kwmu.add("num_rr_tracks",  num_rr_tracks);
	kwmu.add("num_samples",    num_samples);
	kwmu.add("num_conn_tries", num_conn_tries);
	kwmu.add("plots_to_parks_ratio", park_rate);
	kwmu.add("city_border", city_border); // in heightmap texels/mesh grids
	kwmu.add("road_border", road_border); // in heightmap texels/mesh grids
	kwmu.add("slope_width", slope_width); // in heightmap texels/mesh grids
	kwmb.add("assign_house_plots",     assign_house_plots);
	kwmb.add("new_city_conn_road_alg", new_city_conn_road_alg);
	kwmb.add("convert_model_files",    convert_model_files); // to model3d format; applies to cars, people, building items, etc.
	// cars / pedestrians
	kwmu.add("num_cars", num_cars);
	kwmb.add("enable_car_path_finding", enable_car_path_finding);
	kwmb.add("cars_use_driveways",      cars_use_driveways);
	kwmu.add("num_peds", num_peds);
	kwmu.add("num_building_peds",   num_building_peds);
	kwmb.add("ped_respawn_at_dest", ped_respawn_at_dest);
	// parking lots / trees / detail objects
	kwmu.add("min_park_spaces", min_park_spaces); // with default road parameters, can be up to 28
	kwmu.add("min_park_rows",   min_park_rows  ); // with default road parameters, can be up to 8
	kwmu.add("max_trees_per_plot",   max_trees_per_plot);
	kwmu.add("max_benches_per_plot", max_benches_per_plot);
	// lighting
	kwmu.add("max_lights",      max_lights);
	kwmu.add("max_shadow_maps", max_shadow_maps);
	kwmb.add("car_shadows",     car_shadows);
	if (kwmb.maybe_set_from_fp(str, fp)) return 1;
	if (kwmu.maybe_set_from_fp(str, fp)) return 1;

	if (str == "city_size_min") {
		if (!read_uint(fp, city_size_min)) {return read_error(str);}
		if (city_size_max == 0) {city_size_max = city_size_min;}
		if (city_size_max < city_size_min) {return read_error(str);}
	}
	else if (str == "city_size_max") {
		if (!read_uint(fp, city_size_max)) {return read_error(str);}
		if (city_size_min == 0) {city_size_min = city_size_max;}
		if (city_size_max < city_size_min) {return read_error(str);}
	}
	else if (str == "road_width") {
		if (!read_non_neg_float(fp, road_width)) {return read_error(str);}
	}
	else if (str == "road_spacing") {
		if (!read_non_neg_float(fp, road_spacing)) {return read_error(str);}
	}
	else if (str == "road_spacing_rand") {
		if (!read_non_neg_float(fp, road_spacing_rand)) {return read_error(str);}
	}
	else if (str == "road_spacing_xy_add") {
		if (!read_non_neg_float(fp, road_spacing_xy_add)) {return read_error(str);}
	}
	else if (str == "conn_road_seg_len") {
		if (!read_pos_float(fp, conn_road_seg_len)) {return read_error(str);}
	}
	else if (str == "max_road_slope") {
		if (!read_pos_float(fp, max_road_slope)) {return read_error(str);}
	}
	else if (str == "max_track_slope") {
		if (!read_pos_float(fp, max_track_slope)) {return read_error(str);}
	}
	else if (str == "make_4_way_ints") {
		if (!read_uint(fp, make_4_way_ints) || make_4_way_ints > 3) {return read_error(str);}
	}
	else if (str == "add_transmission_lines") {
		if (!read_uint(fp, add_tlines) || add_tlines > 2) {return read_error(str);}
	}
	else if (str == "residential_probability") {
		if (!read_zero_one_float(fp, residential_probability)) {return read_error(str);}
	}
	// cars
	else if (str == "car_speed") {
		if (!read_non_neg_float(fp, car_speed)) {return read_error(str);}
	}
	else if (str == "traffic_balance_val") {
		if (!read_zero_one_float(fp, traffic_balance_val)) {return read_error(str);}
	}
	else if (str == "new_city_prob") {
		if (!read_zero_one_float(fp, new_city_prob)) {return read_error(str);}
	}
	else if (str == "car_model") { // multiple car models
		city_model_t car_model;
		if (!car_model.read(fp)) {return read_error(str);}
		if (!car_model.check_filename()) {cerr << "Error: car_model file '" << car_model.fn << "' does not exist; skipping" << endl; return 1;} // nonfatal
		car_model_files.push_back(car_model);
		max_eq(max_car_scale, car_model.scale);
	}
	else if (str == "helicopter_model") { // multiple helicopter models
		city_model_t hc_model;
		if (!hc_model.read(fp, 1)) {return read_error(str);} // is_helicopter=1
		if (!hc_model.check_filename()) {cerr << "Error: helicopter_model file '" << hc_model.fn << "' does not exist; skipping" << endl; return 1;} // nonfatal
		hc_model_files.push_back(hc_model);
	}
	// pedestrians
	else if (str == "ped_speed") {
		if (!read_non_neg_float(fp, ped_speed)) {return read_error(str);}
	}
	else if (str == "ped_model") {
		city_model_t ped_model;
		if (!ped_model.read(fp)) {return read_error(str);}
		if (!ped_model.check_filename()) {cerr << "Error: ped_model file '" << ped_model.fn << "' does not exist; skipping" << endl; return 1;} // nonfatal
		ped_model_files.push_back(ped_model); // Note: no ped_model_scale
	}
	// other
	else if (str == "min_park_density") {
		if (!read_zero_one_float(fp, min_park_density)) {return read_error(str);}
	}
	else if (str == "max_park_density") {
		if (!read_zero_one_float(fp, max_park_density)) {return read_error(str);}
	}
	else if (str == "tree_spacing") {
		if (!read_pos_float(fp, tree_spacing)) {return read_error(str);}
	}
	else if (str == "smap_size") {
		if (!read_uint(fp, smap_size) || smap_size > 4096) {return read_error(str);}
	}
	else {
		for (unsigned i = 0; i < NUM_OBJ_MODELS; ++i) { // check for object models
			if (str != model_opt_names[i]) continue;
			if (!add_model(i, fp)) {return read_error(str);}
			return 1; // done
		}
		cout << "Unrecognized city keyword in input file: " << str << endl;
		return 0;
	}
	return 1;
}


void buildings_file_err(string const &str, int &error) {
	cout << "Error reading buildings config option " << str << "." << endl;
	error = 1;
}

bool check_texture_file_exists(string const &filename);

int building_params_t::read_building_texture(FILE *fp, string const &str, int &error, bool check_filename) {
	char strc[MAX_CHARS] = {0};
	if (!read_str(fp, strc)) {buildings_file_err(str, error);}

	if (check_filename && !check_texture_file_exists(strc)) {
		std::cerr << "Warning: Skipping texture '" << strc << "' that can't be loaded" << endl;
		return -1; // texture filename doesn't exist
	}
	int const ret(get_texture_by_name(std::string(strc), 0, tex_inv_y, get_wrap_mir()));
	//cout << "texture filename: " << str << ", ID: " << ret << endl;
	return ret;
}
void building_params_t::read_texture_and_add_if_valid(FILE *fp, string const &str, int &error, vector<unsigned> &tids) {
	// Note: this version doesn't accept numbered texture IDs, but it also doesn't fail on missing files
	int const tid(read_building_texture(fp, str, error, 1)); // check_filename=1
	if (tid >= 0) {tids.push_back(tid);}
}
void read_building_tscale(FILE *fp, tid_nm_pair_t &tex, string const &str, int &error) {
	if (!read_float(fp, tex.tscale_x)) {buildings_file_err(str, error);}
	tex.tscale_y = tex.tscale_x; // uniform
}
void read_building_mat_specular(FILE *fp, string const &str, tid_nm_pair_t &tex, int &error) {
	float mag(0.0), shine(0.0);
	if (read_float(fp, mag) && read_float(fp, shine)) {tex.set_specular(mag, shine);} else {buildings_file_err(str, error);}
}

bool building_params_t::parse_buildings_option(FILE *fp) {

	char strc[MAX_CHARS] = {0};
	if (!read_str(fp, strc)) return 0;
	string const str(strc);
	int error(0);
	// TODO: make these members of city_params_t to avoid recreating them for every option
	kw_to_val_map_t<bool     > kwmb(error, "buildings");
	kw_to_val_map_t<unsigned > kwmu(error, "buildings");
	kw_to_val_map_t<float    > kwmf(error, "buildings");
	kw_to_val_map_t<colorRGBA> kwmc(error, "buildings");
	// global parameters
	kwmb.add("flatten_mesh", flatten_mesh);
	kwmu.add("num_place", num_place);
	kwmu.add("num_tries", num_tries);
	kwmu.add("rand_seed", buildings_rand_seed);
	kwmu.add("max_shadow_maps", max_shadow_maps);
	kwmf.add("ao_factor", ao_factor);
	kwmf.add("sec_extra_spacing", sec_extra_spacing);
	kwmf.add("player_coll_radius_scale", player_coll_radius_scale);
	kwmf.add("max_floorplan_window_xscale", max_fp_wind_xscale);
	kwmf.add("max_floorplan_window_yscale", max_fp_wind_yscale);
	kwmf.add("interior_view_dist_scale", interior_view_dist_scale);
	kwmb.add("tt_only", tt_only);
	kwmb.add("infinite_buildings", infinite_buildings);
	kwmb.add("add_secondary_buildings", add_secondary_buildings);
	kwmb.add("add_office_basements", add_office_basements);
	kwmb.add("enable_people_ai", enable_people_ai);
	// material parameters
	kwmf.add("place_radius", cur_mat.place_radius);
	kwmf.add("max_delta_z", cur_mat.max_delta_z);
	kwmf.add("min_level_height", cur_mat.min_level_height);
	kwmu.add("min_levels", cur_mat.min_levels);
	kwmu.add("max_levels", cur_mat.max_levels);
	kwmf.add("min_flat_side_amt", cur_mat.min_fsa);
	kwmf.add("max_flat_side_amt", cur_mat.max_fsa);
	kwmf.add("min_alt_step_factor", cur_mat.min_asf);
	kwmf.add("max_alt_step_factor", cur_mat.max_asf);
	kwmf.add("min_altitude",  cur_mat.min_alt);
	kwmf.add("max_altitude",  cur_mat.max_alt);
	kwmf.add("max_rot_angle", cur_mat.max_rot_angle);
	kwmb.add("dome_roof",  dome_roof);
	kwmb.add("onion_roof", onion_roof);
	kwmb.add("no_city",    cur_mat.no_city);
	// material textures / colors
	kwmb.add("texture_mirror", tex_mirror);
	kwmb.add("texture_inv_y",  tex_inv_y);
	kwmf.add("side_tscale_x",  cur_mat.side_tex.tscale_x);
	kwmf.add("side_tscale_y",  cur_mat.side_tex.tscale_y);
	kwmf.add("side_color_grayscale_rand", cur_mat.side_color.grayscale_rand);
	kwmf.add("roof_color_grayscale_rand", cur_mat.roof_color.grayscale_rand);
	kwmc.add("side_color_min", cur_mat.side_color.cmin);
	kwmc.add("side_color_max", cur_mat.side_color.cmax);
	kwmc.add("roof_color_min", cur_mat.roof_color.cmin);
	kwmc.add("roof_color_max", cur_mat.roof_color.cmax);
	// windows (mostly per-material)
	kwmf.add("window_xoff", cur_mat.wind_xoff);
	kwmf.add("window_yoff", cur_mat.wind_yoff);
	kwmf.add("wall_split_thresh", wall_split_thresh);
	kwmb.add("add_windows",       cur_mat.add_windows);
	kwmb.add("add_window_lights", cur_mat.add_wind_lights);
	kwmc.add("window_color", cur_mat.window_color);
	kwmc.add("wall_color",   cur_mat.wall_color);
	kwmc.add("ceil_color",   cur_mat.ceil_color);
	kwmc.add("floor_color",  cur_mat.floor_color);
	kwmc.add("house_ceil_color",  cur_mat.house_ceil_color);
	kwmc.add("house_floor_color", cur_mat.house_floor_color);
	// AI logic
	kwmu.add("ai_opens_doors",     ai_opens_doors); // 0=don't open doors, 1=only open if player closed door after path selection; 2=always open doors
	kwmb.add("ai_target_player",   ai_target_player);
	kwmb.add("ai_follow_player",   ai_follow_player);
	kwmu.add("ai_player_vis_test", ai_player_vis_test); // 0=no test, 1=LOS, 2=LOS+FOV, 3=LOS+FOV+lit
	// animals
	kwmu.add("num_rats_min", num_rats_min);
	kwmu.add("num_rats_max", num_rats_max);
	kwmf.add("rat_speed",    rat_speed);
	// gameplay state
	kwmf.add("player_weight_limit", player_weight_limit);
	// special commands
	kwmu.add("probability",              cur_prob); // for building materials
	kwmb.add("add_city_interiors",       add_city_interiors);
	kwmb.add("gen_building_interiors",   gen_building_interiors);
	kwmb.add("enable_rotated_room_geom", enable_rotated_room_geom);
	if (kwmb.maybe_set_from_fp(str, fp)) return 1;
	if (kwmu.maybe_set_from_fp(str, fp)) return 1;
	if (kwmf.maybe_set_from_fp(str, fp)) return 1;
	if (kwmc.maybe_set_from_fp(str, fp)) return 1;

	// material parameters
	if (str == "range_translate") { // x,y only
		if (!(read_float(fp, range_translate.x) && read_float(fp, range_translate.y))) {buildings_file_err(str, error);}
	}
	else if (str == "pos_range") {
		if (!read_cube(fp, cur_mat.pos_range, 1)) {buildings_file_err(str, error);}
	}
	else if (str == "split_prob") {
		if (!read_zero_one_float(fp, cur_mat.split_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "cube_prob") {
		if (!read_zero_one_float(fp, cur_mat.cube_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "round_prob") {
		if (!read_zero_one_float(fp, cur_mat.round_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "alt_step_factor_prob") {
		if (!read_zero_one_float(fp, cur_mat.asf_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "min_sides") {
		if (!read_uint(fp, cur_mat.min_sides)) {buildings_file_err(str, error);}
		if (cur_mat.min_sides < 3) {buildings_file_err(str+" (< 3)", error);}
	}
	else if (str == "max_sides") {
		if (!read_uint(fp, cur_mat.max_sides)) {buildings_file_err(str, error);}
		if (cur_mat.max_sides < 3) {buildings_file_err(str+" (< 3)", error);}
	}
	else if (str == "size_range") {
		if (!read_cube(fp, cur_mat.sz_range)) {buildings_file_err(str, error);}
	}
	// material textures
	else if (str == "side_tscale")  {read_building_tscale(fp, cur_mat.side_tex,  str, error);} // both X and Y
	else if (str == "roof_tscale")  {read_building_tscale(fp, cur_mat.roof_tex,  str, error);} // both X and Y
	else if (str == "wall_tscale")  {read_building_tscale(fp, cur_mat.wall_tex,  str, error);} // both X and Y
	else if (str == "ceil_tscale")  {read_building_tscale(fp, cur_mat.ceil_tex,  str, error);} // both X and Y
	else if (str == "floor_tscale") {read_building_tscale(fp, cur_mat.floor_tex, str, error);} // both X and Y
	else if (str == "house_ceil_tscale")  {read_building_tscale(fp, cur_mat.house_ceil_tex,  str, error);} // both X and Y
	else if (str == "house_floor_tscale") {read_building_tscale(fp, cur_mat.house_floor_tex, str, error);} // both X and Y
	else if (str == "basement_floor_tscale") {read_building_tscale(fp, cur_mat.basement_floor_tex, str, error);} // both X and Y
	// building textures
	// Warning: setting options such as tex_inv_y for textures that have already been loaded will have no effect!
	else if (str == "side_tid"    ) {cur_mat.side_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "side_nm_tid" ) {cur_mat.side_tex.nm_tid  = read_building_texture(fp, str, error);}
	else if (str == "roof_tid"    ) {cur_mat.roof_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "roof_nm_tid" ) {cur_mat.roof_tex.nm_tid  = read_building_texture(fp, str, error);}
	// interiors
	else if (str == "wall_tid"    ) {cur_mat.wall_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "wall_nm_tid" ) {cur_mat.wall_tex.nm_tid  = read_building_texture(fp, str, error);}
	else if (str == "floor_tid"   ) {cur_mat.floor_tex.tid    = read_building_texture(fp, str, error);}
	else if (str == "floor_nm_tid") {cur_mat.floor_tex.nm_tid = read_building_texture(fp, str, error);}
	else if (str == "ceil_tid"    ) {cur_mat.ceil_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "ceil_nm_tid" ) {cur_mat.ceil_tex.nm_tid  = read_building_texture(fp, str, error);}
	else if (str == "house_floor_tid"   ) {cur_mat.house_floor_tex.tid    = read_building_texture(fp, str, error);}
	else if (str == "house_floor_nm_tid") {cur_mat.house_floor_tex.nm_tid = read_building_texture(fp, str, error);}
	else if (str == "house_ceil_tid"    ) {cur_mat.house_ceil_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "house_ceil_nm_tid" ) {cur_mat.house_ceil_tex.nm_tid  = read_building_texture(fp, str, error);}
	else if (str == "basement_floor_tid"   ) {cur_mat.basement_floor_tex.tid    = read_building_texture(fp, str, error);}
	else if (str == "basement_floor_nm_tid") {cur_mat.basement_floor_tex.nm_tid = read_building_texture(fp, str, error);}
	else if (str == "open_door_prob") {
		if (!read_zero_one_float(fp, open_door_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "locked_door_prob") {
		if (!read_zero_one_float(fp, locked_door_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "basement_prob") {
		if (!read_zero_one_float(fp, basement_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "ball_prob") {
		if (!read_zero_one_float(fp, ball_prob)) {buildings_file_err(str, error);}
	}
	// material colors
	else if (str == "side_color") {
		if (!read_color(fp, cur_mat.side_color.cmin)) {buildings_file_err(str, error);}
		cur_mat.side_color.cmax = cur_mat.side_color.cmin; // same
	}
	else if (str == "roof_color") {
		if (!read_color(fp, cur_mat.roof_color.cmin)) {buildings_file_err(str, error);}
		cur_mat.roof_color.cmax = cur_mat.roof_color.cmin; // same
	}
	// specular
	else if (str == "side_specular" ) {read_building_mat_specular(fp, str, cur_mat.side_tex,  error);}
	else if (str == "roof_specular" ) {read_building_mat_specular(fp, str, cur_mat.roof_tex,  error);}
	else if (str == "wall_specular" ) {read_building_mat_specular(fp, str, cur_mat.wall_tex,  error);}
	else if (str == "ceil_specular" ) {read_building_mat_specular(fp, str, cur_mat.ceil_tex,  error);}
	else if (str == "floor_specular") {read_building_mat_specular(fp, str, cur_mat.floor_tex, error);}
	else if (str == "house_ceil_specular" ) {read_building_mat_specular(fp, str, cur_mat.house_ceil_tex,  error);}
	else if (str == "house_floor_specular") {read_building_mat_specular(fp, str, cur_mat.house_floor_tex, error);}
	// windows
	else if (str == "window_width") {
		if (!read_zero_one_float(fp, window_width)) {buildings_file_err(str, error);}
	}
	else if (str == "window_height") {
		if (!read_zero_one_float(fp, window_height)) {buildings_file_err(str, error);}
	}
	else if (str == "window_xspace") {
		if (!read_zero_one_float(fp, window_xspace)) {buildings_file_err(str, error);}
	}
	else if (str == "window_yspace") {
		if (!read_zero_one_float(fp, window_yspace)) {buildings_file_err(str, error);}
	}
	else if (str == "window_xscale") {
		if (!read_non_neg_float(fp, cur_mat.wind_xscale)) {buildings_file_err(str, error);}
	}
	else if (str == "window_yscale") {
		if (!read_non_neg_float(fp, cur_mat.wind_yscale)) {buildings_file_err(str, error);}
	}
	else if (str == "house_prob") { // per-material
		if (!read_zero_one_float(fp, cur_mat.house_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "house_scale_range") { // per-material
		if (!read_float(fp, cur_mat.house_scale_min) || !read_float(fp, cur_mat.house_scale_max)) {buildings_file_err(str, error);}
	}
	// room objects/textures
	else if (str == "add_rug_texture"    ) {read_texture_and_add_if_valid(fp, str, error, rug_tids    );}
	else if (str == "add_picture_texture") {read_texture_and_add_if_valid(fp, str, error, picture_tids);}
	else if (str == "add_desktop_texture") {read_texture_and_add_if_valid(fp, str, error, desktop_tids);}
	else if (str == "add_sheet_texture"  ) {read_texture_and_add_if_valid(fp, str, error, sheet_tids  );}
	else if (str == "add_paper_texture"  ) {read_texture_and_add_if_valid(fp, str, error, paper_tids  );}
	// special commands
	else if (str == "add_material") {add_cur_mat();}
	else {
		cout << "Unrecognized buildings keyword in input file: " << str << endl;
		error = 1;
	}
	return !error;
}

bool parse_buildings_option(FILE *fp) {return global_building_params.parse_buildings_option(fp);}


void building_params_t::add_cur_mat() {
	unsigned const mat_ix(materials.size());

	for (unsigned n = 0; n < cur_prob; ++n) { // add more references to this mat for higher probability
		mat_gen_ix.push_back(mat_ix);
		(cur_mat.no_city ? mat_gen_ix_nocity : mat_gen_ix_city).push_back(mat_ix);
		if (cur_mat.house_prob > 0.0) {mat_gen_ix_res.push_back(mat_ix);}
	}
	materials.push_back(cur_mat);
	materials.back().finalize();
	materials.back().update_range(range_translate);
	has_normal_map |= cur_mat.has_normal_map();
}
unsigned building_params_t::choose_rand_mat(rand_gen_t &rgen, bool city_only, bool non_city_only, bool residential) const {
	vector<unsigned> const &mat_ix_list(get_mat_list(city_only, non_city_only, residential));
	assert(!mat_ix_list.empty());
	return mat_ix_list[rgen.rand()%mat_ix_list.size()];
}
float building_params_t::get_max_house_size() const {
	float max_sz(0.0);

	for (auto m = materials.begin(); m != materials.end(); ++m) {
		if (m->house_prob > 0.0) {max_eq(max_sz, m->house_scale_max*max(m->sz_range.x2(), m->sz_range.y2()));} // take max of x/y size upper bounds
	}
	return max_sz;
}
void building_params_t::set_pos_range(cube_t const &pos_range) {
	//cout << "pos_range: " << pos_range.str() << endl;
	cur_mat.set_pos_range(pos_range);
	for (auto i = materials.begin(); i != materials.end(); ++i) {i->set_pos_range(pos_range);}
}
void building_params_t::restore_prev_pos_range() {
	cur_mat.restore_prev_pos_range();
	for (auto i = materials.begin(); i != materials.end(); ++i) {i->restore_prev_pos_range();}
}
void building_params_t::finalize() {
	//if (materials.empty()) {add_cur_mat();} // add current (maybe default) material - seems not to be needed
}

void building_mat_t::update_range(vector3d const &range_translate) {
	if (place_radius > 0.0) { // clip range to place_radius
		point const center(pos_range.get_cube_center());

		for (unsigned d = 0; d < 2; ++d) { // x,y
			max_eq(pos_range.d[d][0], (center[d] - place_radius));
			min_eq(pos_range.d[d][1], (center[d] + place_radius));
		}
	}
	pos_range += range_translate;
}

void color_range_t::gen_color(colorRGBA &color, rand_gen_t &rgen) const {
	if (cmin == cmax) {color = cmin;} // single exact color
	else {UNROLL_4X(color[i_] = rgen.rand_uniform(cmin[i_], cmax[i_]);)}
	if (grayscale_rand > 0.0) {
		float const v(grayscale_rand*rgen.rand_float());
		UNROLL_3X(color[i_] += v;)
	}
}

// windows are scaled to make the texture look correct; this is fine for exterior building wall materials that have no windows, since we can place the windows however we want;
// but some office buildings have windows spaced too close together, and we don't have control over it here;
// so instead we use a larger window space for floorplanning, since windows aren't cut out of the walls anyway
float building_mat_t::get_window_tx() const {return wind_xscale*global_building_params.get_window_tx();}
float building_mat_t::get_window_ty() const {return wind_yscale*global_building_params.get_window_ty();}

void building_mat_t::finalize() { // compute and cache spacing values
	if (!global_building_params.windows_enabled()) return; // don't set the variables below
	float tx(get_window_tx()), ty(get_window_ty());
	if (global_building_params.max_fp_wind_yscale > 0.0) {min_eq(ty, global_building_params.max_fp_wind_yscale*global_building_params.get_window_ty());}
	if (global_building_params.max_fp_wind_xscale > 0.0) {min_eq(tx, global_building_params.max_fp_wind_xscale*global_building_params.get_window_tx());}
	floor_spacing = 1.0/(2.0*ty);
	floorplan_wind_xscale = 2.0f*tx;
}

