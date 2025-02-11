// 3D World - Building Interior Room Object Placement
// by Frank Gennari 7/15/2023

#include "function_registry.h"
#include "buildings.h"
#include "city.h" // for object_model_loader_t

bool const PLACE_LIGHTS_ON_SKYLIGHTS = 1;

extern building_params_t global_building_params;
extern object_model_loader_t building_obj_model_loader;
extern bldg_obj_type_t bldg_obj_types[];

bool enable_parked_cars();
car_t car_from_parking_space(room_object_t const &o);
void get_stove_burner_locs(room_object_t const &stove, point locs[4]);
float get_cockroach_height_from_radius(float radius);
string gen_random_full_name(rand_gen_t &rgen);


class door_path_checker_t {
	vector<point> door_centers;
public:
	bool check_door_path_blocked(cube_t const &c, cube_t const &room, float zval, building_t const &building) {
		if (door_centers.empty()) {building.get_all_door_centers_for_room(room, zval, door_centers);}
		if (door_centers.size() < 2) return 0; // must have at least 2 doors for the path to be blocked

		for (auto p1 = door_centers.begin(); p1 != door_centers.end(); ++p1) {
			for (auto p2 = p1+1; p2 != door_centers.end(); ++p2) {
				if (check_line_clip(*p1, *p2, c.d)) return 1;
			}
		}
		return 0;
	}
	void clear() {door_centers.clear();} // to allow for reuse across rooms
};

bool building_t::is_obj_placement_blocked(cube_t const &c, cube_t const &room, bool inc_open_doors, bool check_open_dir) const {
	if (is_cube_close_to_doorway(c, room, 0.0, inc_open_doors, check_open_dir)) return 1; // too close to a doorway
	if (interior && interior->is_blocked_by_stairs_or_elevator(c))              return 1; // faster to check only one per stairwell, but then we need to store another vector?
	if (!check_cube_within_part_sides(c)) return 1; // handle non-cube buildings
	return 0;
}
bool building_t::is_valid_placement_for_room(cube_t const &c, cube_t const &room, vect_cube_t const &blockers, bool inc_open_doors, float room_pad) const {
	cube_t place_area(room);
	if (room_pad != 0.0f) {place_area.expand_by_xy(-room_pad);} // shrink by dmin
	if (!place_area.contains_cube_xy(c))                   return 0; // not contained in interior part of the room
	if (is_obj_placement_blocked(c, room, inc_open_doors)) return 0;
	if (has_bcube_int(c, blockers))                        return 0; // Note: ignores dmin
	if (has_attic() && c.intersects_xy(interior->attic_access) && (c.z2() + get_window_vspace()) > interior->attic_access.z1()) return 0; // blocked by attic access door (when open)
	return 1;
}

float get_radius_for_square_model(unsigned model_id) {
	vector3d const chair_sz(building_obj_model_loader.get_model_world_space_size(model_id));
	return 0.5f*(chair_sz.x + chair_sz.y)/chair_sz.z; // assume square and take average of xsize and ysize
}

bool building_t::add_chair(rand_gen_t &rgen, cube_t const &room, vect_cube_t const &blockers, unsigned room_id, point const &place_pos,
	colorRGBA const &chair_color, bool dim, bool dir, float tot_light_amt, bool office_chair_model, bool enable_rotation)
{
	if (!building_obj_model_loader.is_model_valid(OBJ_MODEL_OFFICE_CHAIR)) {office_chair_model = 0;}
	float const window_vspacing(get_window_vspace()), room_pad(4.0f*get_wall_thickness()), chair_height(0.4*window_vspacing);
	float chair_hwidth(0.0), push_out(0.0), min_push_out(0.0);
	point chair_pos(place_pos); // same starting center and z1

	if (office_chair_model) {
		chair_hwidth = 0.5f*chair_height*get_radius_for_square_model(OBJ_MODEL_OFFICE_CHAIR);
		min_push_out = 0.5;
		push_out     = min_push_out + rgen.rand_uniform(0.0, 0.6); // pushed out a bit so that the arms don't intersect the table top, but can push out more
	}
	else {
		chair_hwidth = 0.1*window_vspacing; // half width
		min_push_out = -0.5;
		push_out     = min_push_out + rgen.rand_uniform(0.0, 1.7); // varible amount of pushed in/out
	}
	chair_pos[dim] += (dir ? -1.0f : 1.0f)*push_out*chair_hwidth;
	cube_t chair(get_cube_height_radius(chair_pos, chair_hwidth, chair_height));
	
	if (!is_valid_placement_for_room(chair, room, blockers, 0, room_pad)) { // check proximity to doors
		float const max_push_in((dir ? -1.0f : 1.0f)*(min_push_out - push_out)*chair_hwidth);
		chair.translate_dim(dim, max_push_in*rgen.rand_uniform(0.5, 1.0)); // push the chair mostly in and try again
		if (!is_valid_placement_for_room(chair, room, blockers, 0, room_pad)) return 0;
	}
	vect_room_object_t &objs(interior->room_geom->objs);

	if (office_chair_model) {
		unsigned const flags(enable_rotation ? RO_FLAG_RAND_ROT : 0);
		float const lum(chair_color.get_weighted_luminance()); // calculate grayscale luminance
		objs.emplace_back(chair, TYPE_OFF_CHAIR, room_id, dim, dir, flags, tot_light_amt, SHAPE_CUBE, colorRGBA(lum, lum, lum));
	}
	else {
		objs.emplace_back(chair, TYPE_CHAIR, room_id, dim, dir, 0, tot_light_amt, SHAPE_CUBE, chair_color);
	}
	return 1;
}

// Note: must be first placed objects; returns the number of total objects added (table + optional chairs)
unsigned building_t::add_table_and_chairs(rand_gen_t rgen, cube_t const &room, vect_cube_t const &blockers, unsigned room_id,
	point const &place_pos, colorRGBA const &chair_color, float rand_place_off, float tot_light_amt)
{
	float const window_vspacing(get_window_vspace()), room_pad(max(4.0f*get_wall_thickness(), get_min_front_clearance_inc_people()));
	vector3d const room_sz(room.get_size());
	vect_room_object_t &objs(interior->room_geom->objs);
	point table_pos(place_pos);
	vector3d table_sz;
	for (unsigned d = 0; d < 2; ++d) {table_sz [d]  = 0.18*window_vspacing*(1.0 + rgen.rand_float());} // half size relative to window_vspacing
	for (unsigned d = 0; d < 2; ++d) {table_pos[d] += rand_place_off*room_sz[d]*rgen.rand_uniform(-1.0, 1.0);} // near the center of the room
	bool const is_round((rgen.rand()&3) == 0); // 25% of the time
	if (is_round) {table_sz.x = table_sz.y = 0.6f*(table_sz.x + table_sz.y);} // round tables must have square bcubes for now (no oval tables yet); make radius slightly larger
	point llc(table_pos - table_sz), urc(table_pos + table_sz);
	llc.z = table_pos.z; // bottom
	urc.z = table_pos.z + rgen.rand_uniform(0.20, 0.22)*window_vspacing; // top
	cube_t table(llc, urc);
	if (!is_valid_placement_for_room(table, room, blockers, 0, room_pad)) return 0; // check proximity to doors and collision with blockers
	//if (door_path_checker_t().check_door_path_blocked(table, room, table_pos.z, *this)) return 0; // optional, but we may want to allow this for kitchens and dining rooms
	objs.emplace_back(table, TYPE_TABLE, room_id, 0, 0, (is_house ? RO_FLAG_IS_HOUSE : 0), tot_light_amt, (is_round ? SHAPE_CYLIN : SHAPE_CUBE));
	set_obj_id(objs);
	unsigned num_added(1); // start with the table

	// place some chairs around the table
	for (unsigned dim = 0; dim < 2; ++dim) {
		for (unsigned dir = 0; dir < 2; ++dir) {
			if (rgen.rand_bool()) continue; // 50% of the time
			point chair_pos(table_pos); // same starting center and z1
			chair_pos[dim] += (dir ? -1.0f : 1.0f)*table_sz[dim];
			num_added += add_chair(rgen, room, blockers, room_id, chair_pos, chair_color, dim, dir, tot_light_amt, 0); // office_chair_model=0
		}
	}
	return num_added;
}
void building_t::shorten_chairs_in_region(cube_t const &region, unsigned objs_start) {
	for (auto i = interior->room_geom->objs.begin() + objs_start; i != interior->room_geom->objs.end(); ++i) {
		if (i->type != TYPE_CHAIR || !i->intersects(region)) continue;
		i->z2() -= 0.25*i->dz();
		i->shape = SHAPE_SHORT;
	}
}

void building_t::get_doorways_for_room(cube_t const &room, float zval, vect_door_stack_t &doorways) const { // interior doorways; thread safe
	// find interior doorways connected to this room
	float const floor_thickness(get_floor_thickness());
	cube_t room_exp(room);
	room_exp.expand_by_xy(get_wall_thickness());
	set_cube_zvals(room_exp, (zval + floor_thickness), (zval + get_window_vspace() - floor_thickness)); // clip to z-range of this floor
	doorways.clear();

	for (auto i = interior->door_stacks.begin(); i != interior->door_stacks.end(); ++i) {
		if (i->on_stairs) continue; // skip basement door
		if (i->intersects(room_exp)) {doorways.push_back(*i);}
	}
}
vect_door_stack_t &building_t::get_doorways_for_room(cube_t const &room, float zval) const { // interior doorways; not thread safe
	static vect_door_stack_t doorways; // reuse across rooms
	get_doorways_for_room(room, zval, doorways);
	return doorways;
}
void building_t::get_all_door_centers_for_room(cube_t const &room, float zval, vector<point> &door_centers) const {
	float const floor_spacing(get_window_vspace());
	zval += 0.01*floor_spacing; // shift up so that it intersects objects even if they're placed with their z1 exactly at zval
	vect_door_stack_t const &doorways(get_doorways_for_room(room, zval)); // get interior doors
	for (door_stack_t const &ds : doorways) {door_centers.emplace_back(ds.xc(), ds.yc(), zval);}

	if (zval < (ground_floor_z1 + 0.5*floor_spacing)) { // get exterior doors if on the ground floor
		cube_t room_exp(room);
		room_exp.expand_by_xy(get_wall_thickness());

		for (tquad_with_ix_t const &door : doors) {
			cube_t door_bcube(door.get_bcube());
			if (door_bcube.intersects(room_exp)) {door_centers.emplace_back(door_bcube.xc(), door_bcube.yc(), zval);}
		}
	}
}

bool building_t::is_room_an_exit(cube_t const &room, int room_ix, float zval) const { // for living rooms, etc.
	if (is_room_adjacent_to_ext_door(room, 1)) return 1; // front_door_only=1
	if (!multi_family) return 0; // stairs check is only for multi-family houses
	int const has_stairs(room_or_adj_room_has_stairs(room_ix, zval, 1, 0)); // inc_adj_rooms=1, check_door_open=0
	return (has_stairs == 2); // only if adjacent to stairs
}

void building_t::add_trashcan_to_room(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, bool check_last_obj) {
	int const rr(rgen.rand()%3), rar(rgen.rand()%3); // three sizes/ARs
	float const floor_spacing(get_window_vspace()), radius(0.02f*(3 + rr)*floor_spacing), height(0.55f*(3 + rar)*radius); // radius={0.06, 0.08, 0.10} x AR={1.65, 2.2, 2.75}
	cube_t room_bounds(get_walkable_room_bounds(room));
	room_bounds.expand_by_xy(-1.1*radius); // leave a slight gap between trashcan and wall
	if (!room_bounds.is_strictly_normalized()) return; // no space for trashcan (likely can't happen)
	int const floor_ix(int((zval - room.z1())/floor_spacing));
	bool const cylin(((mat_ix + 13*real_num_parts + 5*hallway_dim + 131*floor_ix) % 7) < 4); // varies per-building, per-floor
	point center;
	center.z = zval + 0.0012*floor_spacing; // slightly above the floor/rug to avoid z-fighting
	unsigned skip_wall(4); // start at an invalid value
	vect_door_stack_t const &doorways(get_doorways_for_room(room, zval));
	vect_room_object_t &objs(interior->room_geom->objs);
	cube_t avoid;

	if (!objs.empty() && objs[objs_start].type == TYPE_TABLE) { // make sure there's enough space for the playerand AIs to walk around the table
		avoid = objs[objs_start];
		avoid.expand_by_xy(get_min_front_clearance_inc_people());
	}
	if (check_last_obj) {
		assert(!objs.empty());
		skip_wall = 2*objs.back().dim + (!objs.back().dir); // don't place trashcan on same wall as whiteboard (dir is opposite)
	}
	for (unsigned n = 0; n < 20; ++n) { // make 20 attempts to place a trashcan
		bool dim(rgen.rand_bool()), dir(rgen.rand_bool()); // choose a random wall
		if ((2U*dim + dir) == skip_wall) {dir ^= 1;} // don't place a trashcan on this wall, try opposite wall
		center[dim] = room_bounds.d[dim][dir]; // against this wall
		bool is_good(0);

		for (unsigned m = 0; m < 40; ++m) { // try to find a point near a doorway
			center[!dim] = rgen.rand_uniform(room_bounds.d[!dim][0], room_bounds.d[!dim][1]);
			if (doorways.empty()) break; // no doorways, keep this point
				
			for (auto i = doorways.begin(); i != doorways.end(); ++i) {
				float const dmin(radius + i->dx() + i->dy()), dist_sq(p2p_dist_sq(center, i->closest_pt(center)));
				if (dist_sq > 4.0*dmin*dmin) continue; // too far
				if (dist_sq <     dmin*dmin) {is_good = 0; break;} // too close, reject this point
				is_good = 1; // close enough, keep this point
			}
			if (is_good) break; // done; may never get here if no points are good, but the code below will handle that
		} // for m
		cube_t const c(get_cube_height_radius(center, radius, height));
		if (!avoid.is_all_zeros() && c.intersects_xy(avoid)) continue; // bad placement
		if (is_obj_placement_blocked(c, room, !room.is_hallway) || overlaps_other_room_obj(c, objs_start)) continue; // bad placement
		objs.emplace_back(c, TYPE_TCAN, room_id, dim, dir, 0, tot_light_amt, (cylin ? SHAPE_CYLIN : SHAPE_CUBE), tcan_colors[rgen.rand()%NUM_TCAN_COLORS]);
		return; // done
	} // for n
}

// Note: no blockers, but does check existing objects
bool building_t::add_bookcase_to_room(rand_gen_t &rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, bool is_basement) {
	cube_t room_bounds(get_walkable_room_bounds(room));
	room_bounds.expand_by_xy(-get_trim_thickness());
	float const vspace(get_window_vspace());
	if (min(room_bounds.dx(), room_bounds.dy()) < 1.0*vspace) return 0; // room is too small
	rand_gen_t rgen2;
	rgen2.set_state((room_id + 1), (13*mat_ix + interior->rooms.size() + 1)); // local rgen that's per-building/room; ensures bookcases are all the same size in a library
	float const width(0.4*vspace*rgen2.rand_uniform(1.0, 1.2)), depth(0.12*vspace*rgen2.rand_uniform(1.0, 1.2)), height(0.7*vspace*rgen2.rand_uniform(1.0, 1.2));
	float const clearance(max(0.2f*vspace, get_min_front_clearance_inc_people()));
	vect_room_object_t &objs(interior->room_geom->objs);
	cube_t c;
	set_cube_zvals(c, zval, zval+height);

	for (unsigned n = 0; n < 20; ++n) { // make 20 attempts to place a bookcase
		bool const dim(rgen.rand_bool()), dir(rgen.rand_bool()); // choose a random wall
		if (!is_basement && classify_room_wall(room, zval, dim, dir, 0) == ROOM_WALL_EXT) continue; // don't place against an exterior wall/window, inc. partial ext walls
		c.d[dim][ dir] = room_bounds.d[dim][dir]; // against this wall
		c.d[dim][!dir] = c.d[dim][dir] + (dir ? -1.0 : 1.0)*depth;
		float const pos(rgen.rand_uniform(room_bounds.d[!dim][0]+0.5*width, room_bounds.d[!dim][1]-0.5*width));
		set_wall_width(c, pos, 0.5*width, !dim);
		cube_t tc(c);
		tc.d[dim][!dir] += (dir ? -1.0 : 1.0)*clearance; // increase space to add clearance
		if (is_obj_placement_blocked(tc, room, 1) || overlaps_other_room_obj(tc, objs_start)) continue; // bad placement
		objs.emplace_back(c, TYPE_BCASE, room_id, dim, !dir, 0, tot_light_amt); // Note: dir faces into the room, not the wall
		set_obj_id(objs);
		return 1; // done/success
	} // for n
	return 0; // not placed
}

bool building_t::room_has_stairs_or_elevator(room_t const &room, float zval, unsigned floor) const {
	if (room.has_elevator) return 1; // elevator shafts extend through all rooms in a stack, don't need to check zval
	if (!room.has_stairs_on_floor(floor)) return 0; // no stairs
	assert(interior);
	cube_t c(room);
	set_cube_zvals(c, zval, zval+0.9*get_window_vspace());

	for (auto s = interior->stairwells.begin(); s != interior->stairwells.end(); ++s) {
		if (s->intersects(c)) return 1;
	}
	return 0;
}
bool building_t::is_room_office_bathroom(room_t &room, float zval, unsigned floor) const { // Note: may also update room flags
	if (!room.is_office || room.get_room_type(floor) != RTYPE_BATH) return 0;
	if (!room_has_stairs_or_elevator(room, zval, floor))            return 1;
	room.rtype[wrap_room_floor(floor)] = RTYPE_NOTSET; // not a bathroom; can't call assign_to() because it skips bathrooms
	return 0;
}

// Note: must be first placed object
bool building_t::add_desk_to_room(rand_gen_t rgen, room_t const &room, vect_cube_t const &blockers, colorRGBA const &chair_color,
	float zval, unsigned room_id, unsigned floor, float tot_light_amt, unsigned objs_start, bool is_basement)
{
	cube_t const room_bounds(get_walkable_room_bounds(room));
	float const vspace(get_window_vspace());
	if (min(room_bounds.dx(), room_bounds.dy()) < 1.0*vspace) return 0; // room is too small
	float const width(0.8*vspace*rgen.rand_uniform(1.0, 1.2)), depth(0.38*vspace*rgen.rand_uniform(1.0, 1.2)), height(0.21*vspace*rgen.rand_uniform(1.0, 1.2));
	float const clearance(max(0.5f*depth, get_min_front_clearance_inc_people()));
	vect_room_object_t &objs(interior->room_geom->objs);
	cube_t c;
	set_cube_zvals(c, zval, zval+height);

	for (unsigned n = 0; n < 20; ++n) { // make 20 attempts to place a desk
		bool const dim(rgen.rand_bool()), dir(rgen.rand_bool()); // choose a random wall
		float const dsign(dir ? -1.0 : 1.0);
		c.d[dim][ dir] = room_bounds.d[dim][dir] + rgen.rand_uniform(0.1, 1.0)*dsign*get_wall_thickness(); // almost against this wall
		c.d[dim][!dir] = c.d[dim][dir] + dsign*depth;
		float const pos(rgen.rand_uniform(room_bounds.d[!dim][0]+0.5*width, room_bounds.d[!dim][1]-0.5*width));
		set_wall_width(c, pos, 0.5*width, !dim);
		cube_t desk_pad(c);
		desk_pad.d[dim][!dir] += dsign*clearance; // ensure clearance in front of the desk so that a chair can be placed
		if (!is_valid_placement_for_room(desk_pad, room, blockers, 1)) continue; // check proximity to doors and collision with blockers
		if (overlaps_other_room_obj(desk_pad, objs_start))             continue; // check other objects (for bedroom desks or multiple office desks)
		// make short if against an exterior wall, in an office, or if there's a complex floorplan (in case there's no back wall)
		bool const is_tall(!room.is_office && !has_complex_floorplan && rgen.rand_float() < 0.5 && (is_basement || classify_room_wall(room, zval, dim, dir, 0) != ROOM_WALL_EXT));
		unsigned const desk_obj_ix(objs.size());
		objs.emplace_back(c, TYPE_DESK, room_id, dim, !dir, 0, tot_light_amt, (is_tall ? SHAPE_TALL : SHAPE_CUBE));
		set_obj_id(objs);
		bool const add_computer(building_obj_model_loader.is_model_valid(OBJ_MODEL_TV) && rgen.rand_bool());

		if (add_computer) {
			// add a computer monitor using the TV model
			vector3d const sz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_TV)); // D, W, H
			float const tv_height(1.1*height), tv_hwidth(0.5*tv_height*sz.y/sz.z), tv_depth(tv_height*sz.x/sz.z), center(c.get_center_dim(!dim));
			cube_t tv;
			set_cube_zvals(tv, c.z2(), c.z2()+tv_height);
			tv.d[dim][ dir] = c. d[dim][dir] + dsign*0.25*depth; // 25% of the way from the wall
			tv.d[dim][!dir] = tv.d[dim][dir] + dsign*tv_depth;
			set_wall_width(tv, center, tv_hwidth, !dim);
			objs.emplace_back(tv, TYPE_MONITOR, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_SHORT, BLACK); // monitors are shorter than TVs
			set_obj_id(objs);
			// add a keyboard as well
			float const kbd_hwidth(0.7*tv_hwidth), kbd_depth(0.6*kbd_hwidth), kbd_height(0.06*kbd_hwidth);
			cube_t keyboard;
			set_cube_zvals(keyboard, c.z2(), c.z2()+kbd_height);
			keyboard.d[dim][!dir] = c.d[dim][!dir] - dsign*0.06*depth; // close to front edge
			keyboard.d[dim][ dir] = keyboard.d[dim][!dir] - dsign*kbd_depth;
			set_wall_width(keyboard, center, kbd_hwidth, !dim);
			objs.emplace_back(keyboard, TYPE_KEYBOARD, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt); // add as white, will be drawn with gray/black texture
			// add a computer tower under the desk
			float const cheight(0.75*height), cwidth(0.44*cheight), cdepth(0.9*cheight); // fixed AR=0.44 to match the texture
			bool const comp_side(rgen.rand_bool());
			float const pos(c.d[!dim][comp_side] + (comp_side ? -1.0 : 1.0)*0.8*cwidth);
			cube_t computer;
			set_cube_zvals(computer, c.z1(), c.z1()+cheight);
			set_wall_width(computer, pos, 0.5*cwidth, !dim);
			computer.d[dim][ dir] = c.d[dim][dir] + dsign*0.5*cdepth;
			computer.d[dim][!dir] = computer.d[dim][dir] + dsign*cdepth;
			objs.emplace_back(computer, TYPE_COMPUTER, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt);
			// force even/odd-ness of obj_id based on comp_side so that we know what side to put the drawers on so that they don't intersect the computer
			if (bool(objs[desk_obj_ix].obj_id & 1) == comp_side) {++objs[desk_obj_ix].obj_id;}
		}
		else { // no computer
			if ((rgen.rand()%3) != 0) { // add sheet(s) of paper 75% of the time
				float const pheight(0.115*vspace), pwidth(0.77*pheight), thickness(0.00025*vspace); // 8.5x11

				if (pheight < 0.5*c.get_sz_dim(dim) && pwidth < 0.5*c.get_sz_dim(!dim)) { // desk is large enough for papers
					cube_t paper;
					set_cube_zvals(paper, c.z2(), c.z2()+thickness); // very thin
					unsigned const num_papers(rgen.rand() % 8); // 0-7

					for (unsigned n = 0; n < num_papers; ++n) { // okay if they overlap
						set_wall_width(paper, rgen.rand_uniform(c.d[ dim][0]+pheight, c.d[ dim][1]-pheight), 0.5*pheight,  dim);
						set_wall_width(paper, rgen.rand_uniform(c.d[!dim][0]+pwidth,  c.d[!dim][1]-pwidth),  0.5*pwidth,  !dim);
						objs.emplace_back(paper, TYPE_PAPER, room_id, dim, !dir, (RO_FLAG_NOCOLL | RO_FLAG_RAND_ROT), tot_light_amt, SHAPE_CUBE, paper_colors[rgen.rand()%NUM_PAPER_COLORS]);
						set_obj_id(objs);
						paper.z2() += thickness; // to avoid Z-fighting if different colors
					} // for n
				}
			}
			float const pp_len(0.077*vspace), pp_dia(0.0028*vspace), edge_space(0.75*pp_len); // ~7.5 inches long

			if (edge_space < 0.25*min(c.dx(), c.dy())) { // desk is large enough for pens/pencils
				float const pp_z1(c.z2() + 0.3f*pp_dia); // move above papers, and avoid self shadow from the desk
				cube_t pp_bcube;
				set_cube_zvals(pp_bcube, pp_z1, pp_z1+pp_dia);
				bool const is_big_office(!is_house && room.is_office && interior->rooms.size() > 40);
				unsigned const num_pp(rgen.rand()&(is_big_office ? 2 : 3)); // 0-3 for houses, 0-2 for big office buildings

				for (unsigned n = 0; n < num_pp; ++n) {
					bool const is_pen(rgen.rand_bool());
					colorRGBA const color(is_pen ? pen_colors[rgen.rand()&3] : pencil_colors[rgen.rand()&1]);
					set_wall_width(pp_bcube, rgen.rand_uniform(c.d[ dim][0]+edge_space, c.d[ dim][1]-edge_space), 0.5*pp_len,  dim);
					set_wall_width(pp_bcube, rgen.rand_uniform(c.d[!dim][0]+edge_space, c.d[!dim][1]-edge_space), 0.5*pp_dia, !dim);
					// Note: no check for overlap with books and potted plants, but that would be complex to add and this case is rare;
					//       computer monitors/keyboards aren't added in this case, and pencils should float above papers, so we don't need to check those
					if (!pp_bcube.is_strictly_normalized()) continue; // too small, likely due to FP error when far from the origin
					objs.emplace_back(pp_bcube, (is_pen ? TYPE_PEN : TYPE_PENCIL), room_id, dim, dir, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN, color);
				} // for n
			}
		}
		if (rgen.rand_float() > 0.05) { // 5% chance of no chair
			point chair_pos;
			chair_pos.z = zval;
			chair_pos[dim]  = c.d[dim][!dir];
			chair_pos[!dim] = pos + rgen.rand_uniform(-0.1, 0.1)*width; // slightly misaligned
			// use office chair models when the desk has a computer monitor; now that occlusion culling works well, it's okay to have a ton of these in office buildings
			bool const office_chair_model(add_computer /*&& is_house*/);
			add_chair(rgen, room, blockers, room_id, chair_pos, chair_color, dim, dir, tot_light_amt, office_chair_model);
		}
		return 1; // done/success
	} // for n
	return 0; // failed
}

bool building_t::add_office_objs(rand_gen_t rgen, room_t const &room, vect_cube_t &blockers, colorRGBA const &chair_color,
	float zval, unsigned room_id, unsigned floor, float tot_light_amt, unsigned objs_start, bool is_basement)
{
	vect_room_object_t &objs(interior->room_geom->objs);
	unsigned const desk_obj_id(objs.size());
	if (!add_desk_to_room(rgen, room, blockers, chair_color, zval, room_id, floor, tot_light_amt, objs_start, is_basement)) return 0;

	if (rgen.rand_float() < 0.5 && !room_has_stairs_or_elevator(room, zval, floor)) { // allow two desks in one office
		assert(objs[desk_obj_id].type == TYPE_DESK);
		blockers.push_back(objs[desk_obj_id]); // temporarily add the previous desk as a blocker for the new desk and its chair
		room_object_t const &maybe_chair(objs.back());
		bool const added_chair(maybe_chair.type == TYPE_CHAIR || maybe_chair.type == TYPE_OFF_CHAIR);
		if (added_chair) {blockers.push_back(maybe_chair);}
		add_desk_to_room(rgen, room, blockers, chair_color, zval, room_id, floor, tot_light_amt, objs_start, is_basement);
		if (added_chair) {blockers.pop_back();} // remove the chair if it was added
		blockers.pop_back(); // remove the first desk blocker
	}
	if (rgen.rand_float() < 0.75) { // maybe place a filing cabinet along a wall
		float const fc_height(rgen.rand_uniform(0.45, 0.6)*get_window_vspace());
		vector3d const fc_sz_scale(rgen.rand_uniform(0.40, 0.45), rgen.rand_uniform(0.25, 0.30), 1.0); // depth, width, height
		cube_t place_area(get_walkable_room_bounds(room));
		place_area.expand_by(-0.25*get_wall_thickness());
		place_obj_along_wall(TYPE_FCABINET, room, fc_height, fc_sz_scale, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 1.0, 1); // door clearance
	}
	return 1;
}

bool building_t::create_office_cubicles(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt) { // assumes no prior placed objects
	if (!room.is_office) return 0; // offices only
	if (!room.interior && (rgen.rand()%3) == 0) return 0; // 66.7% chance for non-interior rooms
	cube_t const room_bounds(get_walkable_room_bounds(room));
	float const floor_spacing(get_window_vspace());
	// Note: we could choose the primary dim based on door placement like in office building bathrooms, but it seems easier to not place cubes by doors
	bool const long_dim(room.dx() < room.dy());
	float const rlength(room_bounds.get_sz_dim(long_dim)), rwidth(room_bounds.get_sz_dim(!long_dim)), midpoint(room_bounds.get_center_dim(!long_dim));
	if (rwidth < 2.5*floor_spacing || rlength < 3.5*floor_spacing) return 0; // not large enough
	unsigned const num_cubes(round_fp(rlength/(rgen.rand_uniform(0.75, 0.9)*floor_spacing))); // >= 4
	float const cube_width(rlength/num_cubes), cube_depth(cube_width*rgen.rand_uniform(0.8, 1.2)); // not quite square
	bool const add_middle_col(rwidth > 4.0*cube_depth + 2.0*get_doorway_width()); // enough to fit 4 rows of cubes and 2 hallways in between
	uint16_t const bldg_id(uint16_t(mat_ix + interior->rooms.size())); // some value that's per-building
	cube_t const &part(get_part_for_room(room));
	vect_room_object_t &objs(interior->room_geom->objs);
	bool const has_office_chair(building_obj_model_loader.is_model_valid(OBJ_MODEL_OFFICE_CHAIR));
	float lo_pos(room_bounds.d[long_dim][0]), chair_height(0.0), chair_radius(0.0);
	cube_t c;
	set_cube_zvals(c, zval, zval+0.425*floor_spacing);
	bool added_cube(0);

	if (has_office_chair) {
		chair_height = 0.425*floor_spacing;
		chair_radius = 0.5f*chair_height*get_radius_for_square_model(OBJ_MODEL_OFFICE_CHAIR);
	}
	for (unsigned n = 0; n < num_cubes; ++n) {
		float const hi_pos(lo_pos + cube_width);
		c.d[long_dim][0] = lo_pos;
		c.d[long_dim][1] = hi_pos;

		for (unsigned is_middle = 0; is_middle < (add_middle_col ? 2U : 1U); ++is_middle) {
			if (is_middle && (n == 0 || n+1 == num_cubes)) continue; // skip end rows for middle section

			for (unsigned dir = 0; dir < 2; ++dir) {
				float const wall_pos(is_middle ? midpoint : room_bounds.d[!long_dim][dir]), dir_sign(dir ? -1.0 : 1.0);
				c.d[!long_dim][ dir] = wall_pos;
				c.d[!long_dim][!dir] = wall_pos + dir_sign*cube_depth;
				cube_t test_cube(c);
				test_cube.d[!long_dim][!dir] += dir_sign*0.5*cube_depth; // allow space for people to enter the cubicle
				if (is_obj_placement_blocked(test_cube, room, 1)) continue; // inc_open_doors=1
				bool const against_window(room.d[!long_dim][dir] == part.d[!long_dim][dir]);
				objs.emplace_back(c, TYPE_CUBICLE, room_id, !long_dim, dir, 0, tot_light_amt, ((against_window && !is_middle) ? SHAPE_SHORT : SHAPE_CUBE));
				objs.back().obj_id = bldg_id;
				added_cube = 1;
				// add colliders to allow the player to enter the cubicle but not cross the side walls
				cube_t c2(c), c3(c), c4(c);
				c2.d[long_dim][0] = hi_pos - 0.06*cube_width;
				c3.d[long_dim][1] = lo_pos + 0.06*cube_width;
				c4.d[!long_dim][!dir] = wall_pos + dir_sign*0.12*cube_depth;
				objs.emplace_back(c2, TYPE_COLLIDER, room_id, !long_dim, dir, RO_FLAG_INVIS, tot_light_amt); // side1
				objs.emplace_back(c3, TYPE_COLLIDER, room_id, !long_dim, dir, RO_FLAG_INVIS, tot_light_amt); // side2
				objs.emplace_back(c4, TYPE_COLLIDER, room_id, !long_dim, dir, RO_FLAG_INVIS, tot_light_amt); // back (against wall)

				if (has_office_chair && (rgen.rand()&3)) { // add office chair 75% of the time
					point center(c.get_cube_center());
					center[!long_dim] += dir_sign*0.2*cube_depth;
					for (unsigned d = 0; d < 2; ++d) {center[d] += 0.15*chair_radius*rgen.signed_rand_float();} // slightly random XY position
					center.z = zval;
					cube_t const chair(get_cube_height_radius(center, chair_radius, chair_height));
					objs.emplace_back(chair, TYPE_OFF_CHAIR, room_id, !long_dim, dir, RO_FLAG_RAND_ROT, tot_light_amt, SHAPE_CUBE, GRAY_BLACK);
				}
			} // for d
		} // for col
		lo_pos = hi_pos;
	} // for n
	return added_cube;
}

bool building_t::check_valid_closet_placement(cube_t const &c, room_t const &room, unsigned objs_start, unsigned bed_ix, float min_bed_space) const {
	if (min_bed_space > 0.0) {
		room_object_t const &bed(interior->room_geom->get_room_object_by_index(bed_ix));
		assert(bed.type == TYPE_BED);
		cube_t bed_exp(bed);
		bed_exp.expand_by_xy(min_bed_space);
		if (c.intersects_xy(bed_exp)) return 0; // too close to bed
	}
	return (!overlaps_other_room_obj(c, objs_start) && !is_cube_close_to_doorway(c, room, 0.0, 1));
}

float get_lamp_width_scale() {
	vector3d const sz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_LAMP)); // L, W, H
	return ((sz == zero_vector) ? 0.0 : 0.5f*(sz.x + sz.y)/sz.z);
}

bool building_t::add_bedroom_objs(rand_gen_t rgen, room_t &room, vect_cube_t const &blockers, colorRGBA const &chair_color, float zval, unsigned room_id,
	unsigned floor, float tot_light_amt, unsigned objs_start, bool room_is_lit, bool is_basement, bool force, light_ix_assign_t &light_ix_assign)
{
	// bedrooms should have at least one window; if windowless/interior, it can't be a bedroom; faster than checking count_ext_walls_for_room(room, zval) > 0
	if (room.interior) return 0;
	vect_room_object_t &objs(interior->room_geom->objs);
	unsigned const bed_obj_ix(objs.size()); // if placed, it will be this index
	if (!add_bed_to_room(rgen, room, blockers, zval, room_id, tot_light_amt, floor, force)) return 0; // it's only a bedroom if there's bed
	assert(bed_obj_ix < objs.size());
	room_object_t const bed(objs[bed_obj_ix]); // deep copy so that we don't need to worry about invalidating the reference below
	float const window_vspacing(get_window_vspace());
	cube_t room_bounds(get_walkable_room_bounds(room)), place_area(room_bounds);
	place_area.expand_by(-get_trim_thickness()); // shrink to leave a small gap
	// closet
	float const doorway_width(get_doorway_width()), floor_thickness(get_floor_thickness()), front_clearance(max(0.6f*doorway_width, get_min_front_clearance_inc_people()));
	float const closet_min_depth(0.65*doorway_width), closet_min_width(1.5*doorway_width), min_dist_to_wall(1.0*doorway_width), min_bed_space(front_clearance);
	unsigned const first_corner(rgen.rand() & 3);
	bool const first_dim(rgen.rand_bool());
	cube_t const part(get_part_for_room(room));
	bool placed_closet(0);
	unsigned closet_obj_id(0);
	bool chk_windows[2][2] = {0}; // precompute which walls are exterior and can have windows, {dim}x{dir}

	if (!is_basement && has_windows()) { // are bedrooms ever placed in the basement?
		for (unsigned d = 0; d < 4; ++d) {chk_windows[d>>1][d&1] = (classify_room_wall(room, zval, (d>>1), (d&1), 0) == ROOM_WALL_EXT);}
	}
	for (unsigned n = 0; n < 4 && !placed_closet; ++n) { // try 4 room corners
		unsigned const corner_ix((first_corner + n)&3);
		bool const xdir(corner_ix&1), ydir(corner_ix>>1);
		point const corner(room_bounds.d[0][xdir], room_bounds.d[1][ydir], zval);

		for (unsigned d = 0; d < 2 && !placed_closet; ++d) { // try both dims
			bool const dim(bool(d) ^ first_dim), dir(dim ? ydir : xdir), other_dir(dim ? xdir : ydir);
			if (room_bounds.get_sz_dim(!dim) < closet_min_width + min_dist_to_wall) continue; // room is too narrow to add a closet here
			if (chk_windows[dim][dir]) continue; // don't place closets against exterior walls where they would block a window
			float const dir_sign(dir ? -1.0 : 1.0), signed_front_clearance(dir_sign*front_clearance);
			float const window_hspacing(get_hspacing_for_part(part, dim));
			cube_t c(corner, corner);
			c.d[0][!xdir] += (xdir ? -1.0 : 1.0)*(dim ? closet_min_width : closet_min_depth);
			c.d[1][!ydir] += (ydir ? -1.0 : 1.0)*(dim ? closet_min_depth : closet_min_width);
			if (chk_windows[!dim][other_dir] && is_val_inside_window(part, dim, c.d[dim][!dir], window_hspacing, get_window_h_border())) continue; // check for window intersection
			c.z2() += window_vspacing - floor_thickness;
			c.d[dim][!dir] += signed_front_clearance; // extra padding in front, to avoid placing too close to bed
			if (!check_valid_closet_placement(c, room, objs_start, bed_obj_ix, min_bed_space)) continue; // bad placement
			// good placement, see if we can make the closet larger
			unsigned const num_steps = 10;
			float const req_dist(chk_windows[!dim][!other_dir] ? (other_dir ? -1.0 : 1.0)*min_dist_to_wall : 0.0); // signed; at least min dist from the opposite wall if it's exterior
			float const max_grow((room_bounds.d[!dim][!other_dir] - req_dist) - c.d[!dim][!other_dir]);
			float const len_step(max_grow/num_steps), depth_step(dir_sign*0.35*doorway_width/num_steps); // signed

			for (unsigned s1 = 0; s1 < num_steps; ++s1) { // try increasing width
				cube_t c2(c);
				c2.d[!dim][!other_dir] += len_step;
				if (!check_valid_closet_placement(c2, room, objs_start, bed_obj_ix, min_bed_space)) break; // bad placement
				c = c2; // valid placement, update with larger cube
			}
			for (unsigned s2 = 0; s2 < num_steps; ++s2) { // now try increasing depth
				cube_t c2(c);
				c2.d[dim][!dir] += depth_step;
				if (chk_windows[!dim][other_dir] && is_val_inside_window(part, dim, (c2.d[dim][!dir] - signed_front_clearance),
					window_hspacing, get_window_h_border())) break; // bad placement
				if (!check_valid_closet_placement(c2, room, objs_start, bed_obj_ix, min_bed_space)) break; // bad placement
				c = c2; // valid placement, update with larger cube
			}
			c.d[dim][!dir] -= signed_front_clearance; // subtract off front clearance
			assert(c.is_strictly_normalized());
			unsigned flags(0);
			if (c.d[!dim][0] == room_bounds.d[!dim][0]) {flags |= RO_FLAG_ADJ_LO;}
			if (c.d[!dim][1] == room_bounds.d[!dim][1]) {flags |= RO_FLAG_ADJ_HI;}
			//if ((rgen.rand() % 10) == 0) {flags |= RO_FLAG_OPEN;} // 10% chance of open closet; unclear if this adds any value, but it works
			closet_obj_id = objs.size();
			objs.emplace_back(c, TYPE_CLOSET, room_id, dim, !dir, flags, tot_light_amt, SHAPE_CUBE, wall_color); // closet door is always white; sides should match interior walls
			set_obj_id(objs);
			if (flags & RO_FLAG_OPEN) {interior->room_geom->expand_object(objs.back(), *this);} // expand opened closets immediately
			placed_closet = 1; // done
			// add a light inside the closet
			room_object_t const &closet(objs.back());
			point lpos(cube_top_center(closet));
			lpos[dim] += 0.05*c.get_sz_dim(dim)*(dir ? -1.0 : 1.0); // move slightly toward the front of the closet
			cube_t light(lpos);
			light.z1() -= 0.02*window_vspacing;
			light.expand_by_xy((closet.is_small_closet() ? 0.04 : 0.06)*window_vspacing);
			colorRGBA const color(1.0, 1.0, 0.9); // yellow-ish
			objs.emplace_back(light, TYPE_LIGHT, room_id, dim, 0, (RO_FLAG_NOCOLL | RO_FLAG_IN_CLOSET), 0.0, SHAPE_CYLIN, color); // dir=0 (unused)
			objs.back().obj_id = light_ix_assign.get_next_ix();

			if (closet.is_small_closet()) { // add a blocker in front of the closet to avoid placing furniture that blocks the door from opening
				c.d[dim][!dir] += dir_sign*doorway_width;
				objs.emplace_back(c, TYPE_BLOCKER, room_id, dim, 0, RO_FLAG_INVIS);
			}
		} // for d
	} // for n
	// dresser
	float const ds_height(rgen.rand_uniform(0.26, 0.32)*window_vspacing), ds_depth(rgen.rand_uniform(0.20, 0.25)*window_vspacing), ds_width(rgen.rand_uniform(0.6, 0.9)*window_vspacing);
	vector3d const ds_sz_scale(ds_depth/ds_height, ds_width/ds_height, 1.0);
	unsigned const dresser_obj_id(objs.size());
	
	if (place_obj_along_wall(TYPE_DRESSER, room, ds_height, ds_sz_scale, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 1.0, 1)) { // door clearance
		room_object_t &dresser(objs[dresser_obj_id]);

		// place a mirror on the dresser 25% of the time; skip if against an exterior wall to avoid blocking a window
		if (rgen.rand_float() < 0.25 && classify_room_wall(room, zval, dresser.dim, !dresser.dir, 0) != ROOM_WALL_EXT) {
			room_object_t mirror(dresser);
			mirror.type = TYPE_DRESS_MIR;
			set_cube_zvals(mirror, dresser.z2(), (dresser.z2() + 1.4*dresser.get_height()));
			mirror.d[mirror.dim][mirror.dir] -= (mirror.dir ? 1.0 : -1.0)*0.9*dresser.get_length(); // push it toward the back
			mirror.expand_in_dim(!mirror.dim, -0.02*mirror.get_width()); // shrink slightly
			if (is_house) {mirror.flags |= RO_FLAG_IS_HOUSE;} // flag as in a house for reflections logic
			//mirror .flags |= RO_FLAG_NOCOLL; // leave this unset so that light switches aren't blocked, etc.
			dresser.flags |= RO_FLAG_ADJ_TOP; // flag the dresser as having an item on it so that we don't add something else that blocks or intersects the mirror
			objs.push_back(mirror);
			set_obj_id(objs); // for crack texture selection/orient
			room.has_mirror = 1;
		}
	}
	// nightstand
	unsigned const pref_orient(2*bed.dim + (!bed.dir)); // prefer the same orient as the bed so that it's placed on the same wall next to the bed
	float const ns_height(rgen.rand_uniform(0.24, 0.26)*window_vspacing), ns_depth(rgen.rand_uniform(0.15, 0.2)*window_vspacing), ns_width(rgen.rand_uniform(1.0, 2.0)*ns_depth);
	vector3d const ns_sz_scale(ns_depth/ns_height, ns_width/ns_height, 1.0);
	place_obj_along_wall(TYPE_NIGHTSTAND, room, ns_height, ns_sz_scale, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 1.0, 1, pref_orient); // door clearance

	if (placed_closet) { // determine if there's space for the closet doors to fold outward
		room_object_t &closet(objs[closet_obj_id]);

		if (closet.get_sz_dim(!closet.dim) < 1.8*closet.dz()) { // only for medium sized closets
			bool const dim(closet.dim), dir(closet.dir);
			cube_t doors_area(closet);
			doors_area.d[dim][!dir]  = closet.d[dim][dir]; // flush with the front of the closet
			doors_area.d[dim][ dir] += (dir ? 1.0 : -1.0)*0.25*closet.get_sz_dim(!dim); // extend outward by a quarter the closet width
			bool can_fold((room_bounds.d[dim][dir] < doors_area.d[dim][dir]) ^ dir); // should be true, unless closet is very wide and room is very narrow

			for (auto i = objs.begin()+objs_start; i != objs.end() && can_fold; ++i) {
				if (i->type == TYPE_CLOSET || i->type == TYPE_LIGHT) continue; // skip the closet and its light
				can_fold &= !i->intersects(doors_area);
			}
			if (can_fold) { // mark as folding
				closet.flags |= RO_FLAG_HANGING;
				objs.emplace_back(doors_area, TYPE_BLOCKER, room_id, dim, dir, RO_FLAG_INVIS); // prevent adding bookcases/trashcans/balls intersecting open closet doors
			}
		}
	}
	// try to place a lamp on a dresser or nightstand that was added to this room
	if (building_obj_model_loader.is_model_valid(OBJ_MODEL_LAMP) && (rgen.rand()&3) != 0) {
		float const height(0.25*window_vspacing), width(height*get_lamp_width_scale());
		point pillow_center(bed.get_cube_center());
		pillow_center[bed.dim] += (bed.dir ? 1.0 : -1.0)*0.5*bed.get_sz_dim(bed.dim); // adjust from bed center to near the pillow(s)
		int obj_id(-1);
		float dmin_sq(0.0);

		for (auto i = objs.begin()+objs_start; i != objs.end(); ++i) { // choose the dresser or nightstand closest to be bed
			if (i->type != TYPE_DRESSER && i->type != TYPE_NIGHTSTAND) continue; // not a dresser or nightstand
			if (i->flags & RO_FLAG_ADJ_TOP)    continue; // don't add a lamp if there's a mirror on it
			if (min(i->dx(), i->dy()) < width) continue; // too small to place a lamp on
			float const dist_sq(p2p_dist_xy_sq(i->get_cube_center(), pillow_center));
			if (dmin_sq == 0.0 || dist_sq < dmin_sq) {obj_id = (i - objs.begin()); dmin_sq = dist_sq;}
		}
		if (obj_id >= 0) { // found a valid object to place this on
			room_object_t &obj(objs[obj_id]);
			point center(obj.get_cube_center());
			center.z = obj.z2();
			cube_t lamp(get_cube_height_radius(center, 0.5*width, height));
			lamp.translate_dim(obj.dim, (obj.dir ? 1.0 : -1.0)*0.1*width); // move slightly toward the front to avoid clipping through the wall
			float const shift_range(0.4f*(obj.get_sz_dim(!obj.dim) - width)), obj_center(obj.get_center_dim(!obj.dim)), targ_pos(pillow_center[!obj.dim]);
			float shift_val(0.0), dmin(0.0);

			for (unsigned n = 0; n < 4; ++n) { // generate several random positions on the top of the object and choose the one closest to the bed
				float const cand_shift(rgen.rand_uniform(-1.0, 1.0)*shift_range), dist(fabs((obj_center + cand_shift) - targ_pos));
				if (dmin == 0.0 || dist < dmin) {shift_val = cand_shift; dmin = dist;}
			}
			lamp.translate_dim(!obj.dim, shift_val);
			unsigned flags(RO_FLAG_NOCOLL); // no collisions, as an optimization since the player and AI can't get onto the dresser/nightstand anyway
			if (rgen.rand_bool() && !room_is_lit) {flags |= RO_FLAG_LIT;} // 50% chance of being lit if the room is dark (Note: don't let room_is_lit affect rgen)
			obj.flags |= RO_FLAG_ADJ_TOP; // flag this object as having something on it
			objs.emplace_back(lamp, TYPE_LAMP, room_id, obj.dim, obj.dir, flags, tot_light_amt, SHAPE_CYLIN, lamp_colors[rgen.rand()%NUM_LAMP_COLORS]); // Note: invalidates obj ref
		}
	}
	if (min(room_bounds.dx(), room_bounds.dy()) > 2.5*window_vspacing && max(room_bounds.dx(), room_bounds.dy()) > 3.0*window_vspacing) {
		// large room, try to add a desk and chair as well
		add_desk_to_room(rgen, room, blockers, chair_color, zval, room_id, floor, tot_light_amt, objs_start, is_basement);
	}
	if (rgen.rand_float() < 0.3) {add_laundry_basket(rgen, room, zval, room_id, tot_light_amt, objs_start, place_area);} // try to place a laundry basket 25% of the time

	if (rgen.rand_float() < global_building_params.ball_prob) { // maybe add a ball to the room
		add_ball_to_room(rgen, room, place_area, zval, room_id, tot_light_amt, objs_start);
	}
	if (building_obj_model_loader.is_model_valid(OBJ_MODEL_CEIL_FAN) && rgen.rand_float() < 0.3) { // maybe add ceiling fan
		// find the ceiling light, which should be the last object placed before calling this function, and center the fan on it
		if (objs_start > 0 && objs[objs_start-1].type == TYPE_LIGHT) {
			vector3d const sz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_CEIL_FAN)); // D, W, H
			float const diameter(min(0.4*min(room.dx(), room.dy()), 0.5*window_vspacing)), height(diameter*sz.z/sz.y); // assumes width = depth = diameter
			room_object_t &light(objs[objs_start-1]);
			point const top_center(light.xc(), light.yc(), (zval + window_vspacing - floor_thickness)); // on the ceiling
			cube_t fan(top_center, top_center);
			fan.expand_by_xy(0.5*diameter);
			fan.z1() -= height;

			if (!placed_closet || !objs[closet_obj_id].intersects(fan)) { // check for closet intersection
				light.translate_dim(2, -0.9*height); // move near the bottom of the ceiling fan (before invalidating with objs.emplace_back())
				light.flags |= RO_FLAG_INVIS;   // don't draw the light itself; assume the light is part of the bottom of the fan instead
				light.flags |= RO_FLAG_HANGING; // don't draw upward facing light
				unsigned flags(RO_FLAG_NOCOLL);
				if (rgen.rand_float() < 0.65) {flags |= RO_FLAG_ROTATING;} // make fan rotate when turned on 65% of the time
				objs.emplace_back(fan, TYPE_CEIL_FAN, room_id, 0, 0, (RO_FLAG_NOCOLL | RO_FLAG_ROTATING), tot_light_amt, SHAPE_CYLIN, WHITE);
				objs.back().obj_id = objs_start-1; // store light index in this object
			}
		}
	}
	if (rgen.rand_float() < 0.3) { // maybe add a t-shirt or jeans on the floor
		unsigned const type(rgen.rand_bool() ? TYPE_PANTS : TYPE_TEESHIRT);
		bool already_on_bed(0);

		for (auto i = objs.begin()+objs_start; i != objs.end(); ++i) {
			if (i->type == type) {already_on_bed = 1; break;}
		}
		if (!already_on_bed) { // if shirt/pants are already on the bed, don't put them on the floor
			float const length(((type == TYPE_TEESHIRT) ? 0.26 : 0.2)*window_vspacing), width(0.98*length), height(0.002*window_vspacing);
			cube_t valid_area(place_area);
			valid_area.expand_by_xy(-0.25*window_vspacing); // not too close to a wall to avoid bookcases, dressers, and nightstands
			bool const dim(rgen.rand_bool()), dir(rgen.rand_bool()); // choose a random orientation
			vector3d size(0.5*length, 0.5*width, height);
			if (dim) {std::swap(size.x, size.y);}

			if (valid_area.dx() > 2.0*size.x && valid_area.dy() > 2.0*size.y) { // should always be true
				for (unsigned n = 0; n < 10; ++n) { // make 10 attempts to place the object
					point const pos(gen_xy_pos_in_area(valid_area, size, rgen, zval));
					cube_t c(pos);
					c.expand_by_xy(size);
					c.z2() += size.z;
					if (overlaps_other_room_obj(c, objs_start) || is_obj_placement_blocked(c, room, 1)) continue; // bad placement
					colorRGBA const &color((type == TYPE_TEESHIRT) ? TSHIRT_COLORS[rgen.rand()%NUM_TSHIRT_COLORS] : WHITE); // T-shirts are colored, jeans are always white
					objs.emplace_back(c, type, room_id, dim, dir, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CUBE, color);
					break; // done
				} // for n
			}
		}
	}
	return 1; // success
}

// Note: must be first placed object
bool building_t::add_bed_to_room(rand_gen_t &rgen, room_t const &room, vect_cube_t const &blockers, float zval, unsigned room_id, float tot_light_amt, unsigned floor, bool force) {
	unsigned const NUM_COLORS = 8;
	colorRGBA const colors[NUM_COLORS] = {WHITE, WHITE, WHITE, LT_BLUE, LT_BLUE, PINK, PINK, LT_GREEN}; // color of the sheets
	cube_t room_bounds(get_walkable_room_bounds(room));
	float const vspace(get_window_vspace()), wall_thick(get_wall_thickness());
	bool const dim(room_bounds.dx() < room_bounds.dy()); // longer dim
	vector3d expand, bed_sz;
	expand[ dim] = -wall_thick; // small amount of space
	expand[!dim] = -0.3f*vspace; // leave at least some space between the bed and the wall
	room_bounds.expand_by_xy(expand);
	float const room_len(room_bounds.get_sz_dim(dim)), room_width(room_bounds.get_sz_dim(!dim));
	
	if (force) { // no room is too large
		if (room_len < 1.0*vspace || room_width < 0.55*vspace) return 0; // room is too small to fit a bed
	}
	else if (floor == 0) { // special case for ground floor
		if (room_len < 1.3*vspace || room_width < 0.7*vspace) return 0; // room is too small to fit a bed
		if (room_len > 4.0*vspace || room_width > 2.5*vspace) return 0; // room is too large to be a bedroom
	}
	else { // more relaxed constraints
		if (room_len < 1.1*vspace || room_width < 0.6*vspace) return 0; // room is too small to fit a bed
		if (room_len > 4.5*vspace || room_width > 3.5*vspace) return 0; // room is too large to be a bedroom
	}
	bool const first_head_dir(rgen.rand_bool()), first_wall_dir(rgen.rand_bool());
	door_path_checker_t door_path_checker;
	vect_room_object_t &objs(interior->room_geom->objs);
	cube_t c;
	c.z1() = zval;

	for (unsigned n = 0; n < (force ? 100U : 20U); ++n) { // make 20 attempts to place a bed
		float const sizes[6][2] = {{38, 75}, {38, 80}, {53, 75}, {60, 80}, {76, 80}, {72, 84}}; // twin, twin XL, full, queen, king, cal king
		unsigned const size_ix((room_width < 0.9*vspace) ? (rgen.rand() % 6) : (2 + (rgen.rand() % 4))); // only add twin beds to narrow rooms
		bed_sz[ dim] = 0.01f*vspace*(sizes[size_ix][1] + 8.0f); // length (mattress + headboard + footboard)
		bed_sz[!dim] = 0.01f*vspace*(sizes[size_ix][0] + 4.0f); // width  (mattress + small gaps)
		if (room_bounds.dx() < 1.5*bed_sz.x || room_bounds.dy() < 1.5*bed_sz.y) continue; // room is too small for a bed of this size
		bed_sz.z = 0.3*vspace*rgen.rand_uniform(1.0, 1.2); // height
		c.z2()   = zval + bed_sz.z;

		for (unsigned d = 0; d < 2; ++d) {
			float const min_val(room_bounds.d[d][0]), max_val(room_bounds.d[d][1] - bed_sz[d]);

			if (bool(d) == dim && n < 5) { // in the first few iterations, try to place the head of the bed against the wall (maybe not for exterior wall facing window?)
				c.d[d][0] = ((first_head_dir ^ bool(n&1)) ? min_val : max_val);
			}
			else if (bool(d) != dim && rgen.rand_bool()) { // try to place the bed against the wall sometimes
				c.d[d][0] = ((first_wall_dir ^ bool(n&1)) ? (min_val - 0.25*vspace) : (max_val + 0.25*vspace));
			}
			else {
				c.d[d][0] = rgen.rand_uniform(min_val, max_val);
			}
			c.d[d][1] = c.d[d][0] + bed_sz[d];
		} // for d
		if (!is_valid_placement_for_room(c, room, blockers, 1)) continue; // check proximity to doors and collision with blockers
		// prefer not to block the path between doors in the first half of iterations
		if (n < 10 && door_path_checker.check_door_path_blocked(c, room, zval, *this)) continue;
		bool const dir((room_bounds.d[dim][1] - c.d[dim][1]) < (c.d[dim][0] - room_bounds.d[dim][0])); // head of the bed is closer to the wall
		objs.emplace_back(c, TYPE_BED, room_id, dim, dir, 0, tot_light_amt);
		set_obj_id(objs);
		room_object_t &bed(objs.back());
		// use white color if a texture is assigned that's not close to white
		int const sheet_tid(bed.get_sheet_tid());
		if (sheet_tid < 0 || sheet_tid == WHITE_TEX || texture_color(sheet_tid).get_luminance() > 0.5) {bed.color = colors[rgen.rand()%NUM_COLORS];}
		cube_t cubes[6]; // frame, head, foot, mattress, pillow, legs_bcube
		get_bed_cubes(bed, cubes);
		cube_t const &mattress(cubes[3]), &pillow(cubes[4]);
		float const rand_val(rgen.rand_float());

		if (rand_val < 0.4) { // add a blanket on the bed 40% of the time
			vector3d const mattress_sz(mattress.get_size());
			cube_t blanket(mattress);
			set_cube_zvals(blanket, mattress.z2(), (mattress.z2() + 0.02*mattress_sz.z)); // on top of mattress; set height
			blanket.d[dim][ dir] = pillow.d[dim][!dir] - (dir ? 1.0 : -1.0)*rgen.rand_uniform(0.01, 0.06)*mattress_sz[dim]; // shrink at head
			blanket.d[dim][!dir] += (dir ? 1.0 : -1.0)*rgen.rand_uniform(0.03, 0.08)*mattress_sz[dim]; // shrink at foot
			blanket.expand_in_dim(!dim, -rgen.rand_uniform(0.08, 0.16)*mattress_sz[!dim]); // shrink width
			objs.emplace_back(blanket, TYPE_BLANKET, room_id, dim, dir, RO_FLAG_NOCOLL, tot_light_amt);
			set_obj_id(objs);
		}
		else if (rand_val < 0.7) { // add teeshirt or jeans on the bed 30% of the time
			unsigned const type(rgen.rand_bool() ? TYPE_PANTS : TYPE_TEESHIRT);
			float const length(((type == TYPE_TEESHIRT) ? 0.26 : 0.2)*vspace), width(0.98*length), height(0.002*vspace);
			cube_t valid_area(mattress);
			valid_area.d[dim][dir] = pillow.d[dim][!dir]; // don't place under the pillow
			bool const dim2(rgen.rand_bool()), dir2(rgen.rand_bool()); // choose a random orientation
			vector3d size(0.5*length, 0.5*width, height);
			if (dim2) {std::swap(size.x, size.y);}

			if (valid_area.dx() > 2.0*size.x && valid_area.dy() > 2.0*size.y) {
				point const pos(gen_xy_pos_in_area(valid_area, size, rgen, mattress.z2()));
				cube_t c(pos);
				c.expand_by_xy(size);
				c.z2() += size.z;
				colorRGBA const &color((type == TYPE_TEESHIRT) ? TSHIRT_COLORS[rgen.rand()%NUM_TSHIRT_COLORS] : WHITE); // T-shirts are colored, jeans are always white
				objs.emplace_back(c, type, room_id, dim2, dir2, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CUBE, color);
			}
		}
		return 1; // done/success
	} // for n
	return 0;
}

bool building_t::add_ball_to_room(rand_gen_t &rgen, room_t const &room, cube_t const &place_area, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start) {
	float const radius(0.048*get_window_vspace()); // 4.7 inches
	cube_t ball_area(place_area);
	ball_area.expand_by_xy(-radius*rgen.rand_uniform(1.0, 10.0));
	vect_room_object_t &objs(interior->room_geom->objs);
	if (!ball_area.is_strictly_normalized()) return 0; // should always be normalized
	float const ceil_zval(zval + get_floor_ceil_gap());
	
	for (unsigned n = 0; n < 10; ++n) { // make 10 attempts to place the object
		point center(0.0, 0.0, (zval + radius));

		if (room.is_ext_basement()) { // office backrooms: place anywhere within the room
			center = gen_xy_pos_in_area(ball_area, radius, rgen, center.z);
		}
		else { // house bedroom: place along a wall
			bool const dim(rgen.rand_bool()), dir(rgen.rand_bool()); // choose a random wall
			center[ dim] = ball_area.d[dim][dir];
			center[!dim] = rgen.rand_uniform(ball_area.d[!dim][0], ball_area.d[!dim][1]); // random position along the wall
		}
		set_float_height(center, radius, ceil_zval); // floats on water
		cube_t c(center);
		c.expand_by(radius);
		if (overlaps_other_room_obj(c, objs_start) || is_obj_placement_blocked(c, room, 1)) continue; // bad placement
		objs.emplace_back(c, TYPE_LG_BALL, room_id, 0, 0, RO_FLAG_DSTATE, tot_light_amt, SHAPE_SPHERE, WHITE);
		objs.back().obj_id     = (uint16_t)interior->room_geom->allocate_dynamic_state(); // allocate a new dynamic state object
		objs.back().item_flags = rgen.rand_bool(); // selects ball type
		return 1; // done
	} // for n
	return 0;
}

colorRGBA gen_vase_color(rand_gen_t &rgen) {
	if (rgen.rand_bool()) return WHITE; // will be textured
	return colorRGBA(rgen.rand_float(), rgen.rand_float(), rgen.rand_float(), 1.0); // solid pastel color
}

// Note: modified blockers rather than using it; fireplace must be the first placed object
bool building_t::maybe_add_fireplace_to_room(rand_gen_t &rgen, room_t const &room, vect_cube_t &blockers, float zval, unsigned room_id, float tot_light_amt) {
	// Note: the first part of the code below is run on every first floor room and will duplicate work, so it may be better to factor it out somehow
	cube_t fireplace(get_fireplace()); // make a copy of the exterior fireplace that will be converted to an interior fireplace
	bool dim(0), dir(0);
	if      (fireplace.x1() <= bcube.x1()) {dim = 0; dir = 0;} // Note: may not work on rotated buildings
	else if (fireplace.x2() >= bcube.x2()) {dim = 0; dir = 1;}
	else if (fireplace.y1() <= bcube.y1()) {dim = 1; dir = 0;}
	else if (fireplace.y2() >= bcube.y2()) {dim = 1; dir = 1;}
	else {assert(is_rotated()); return 0;} // can fail on rotated buildings?
	float const depth_signed((dir ? -1.0 : 1.0)*1.0*fireplace.get_sz_dim(dim)), wall_pos(fireplace.d[dim][!dir]), top_gap(0.15*fireplace.dz());
	fireplace.d[dim][ dir] = wall_pos; // flush with the house wall
	fireplace.d[dim][!dir] = wall_pos + depth_signed; // extend out into the room
	fireplace.z2() -= top_gap; // shorten slightly
	cube_t room_exp(room);
	room_exp.expand_by_xy(0.5*get_wall_thickness()); // allow fireplace to extend slightly into room walls
	if (!room_exp.contains_cube_xy(fireplace)) return 0; // fireplace not in this room
	// the code below should be run at most once per building
	cube_t fireplace_ext(fireplace);
	fireplace_ext.d[dim][!dir] = fireplace.d[dim][!dir] + 0.5*depth_signed; // extend out into the room even further for clearance
	if (interior->is_blocked_by_stairs_or_elevator(fireplace_ext)) return 0; // blocked by stairs, don't add (would be more correct to relocate stairs) - should no longer fail
	fireplace.d[dim][dir] = room.d[dim][dir]; // re-align to room to remove any gap between the fireplace and the exterior wall
	vect_room_object_t &objs(interior->room_geom->objs);
	objs.emplace_back(fireplace, TYPE_FPLACE, room_id, dim, dir, 0, tot_light_amt);
	cube_t blocker(fireplace_ext);
	blocker.d[dim][ dir] = fireplace.d[dim][!dir]; // flush with the front of the fireplace
	objs.emplace_back(blocker, TYPE_BLOCKER, room_id, dim, dir, RO_FLAG_INVIS);
	blockers.push_back(fireplace_ext); // add as a blocker if it's not already there

	if (rgen.rand_bool()) { // add urn on fireplace
		float const urn_height(rgen.rand_uniform(0.65, 0.95)*top_gap), urn_radius(rgen.rand_uniform(0.35, 0.45)*min(urn_height, fabs(depth_signed)));
		point center(fireplace.get_cube_center());
		center[!dim] += 0.45*fireplace.get_sz_dim(!dim)*rgen.signed_rand_float(); // random placement to the left and right of center
		cube_t urn;
		urn.set_from_sphere(center, urn_radius);
		set_cube_zvals(urn, fireplace.z2(), fireplace.z2()+urn_height); // place on the top of the fireplace
		objs.emplace_back(urn, TYPE_URN, room_id, 0, 0, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN, gen_vase_color(rgen));
		set_obj_id(objs);
	}
	has_int_fplace = 1;
	return 1;
}

bool building_t::place_obj_along_wall(room_object type, room_t const &room, float height, vector3d const &sz_scale, rand_gen_t &rgen, float zval,
	unsigned room_id, float tot_light_amt, cube_t const &place_area, unsigned objs_start, float front_clearance, bool add_door_clearance,
	unsigned pref_orient, bool pref_centered, colorRGBA const &color, bool not_at_window, room_obj_shape shape)
{
	float const hwidth(0.5*height*sz_scale.y/sz_scale.z), depth(height*sz_scale.x/sz_scale.z);
	float const min_space(max(2.8f*hwidth, 2.1f*(max(hwidth, 0.5f*depth) + get_scaled_player_radius()))); // make sure the player can get around the object
	vector3d const place_area_sz(place_area.get_size());
	if (max(place_area_sz.x, place_area_sz.y) <= min_space) return 0; // can't fit in either dim
	unsigned const force_dim((place_area_sz.x <= min_space) ? 0 : ((place_area_sz.y <= min_space) ? 1 : 2)); // *other* dim; 2=neither
	float const obj_clearance(depth*front_clearance), clearance(max(obj_clearance, get_min_front_clearance_inc_people()));
	vect_room_object_t &objs(interior->room_geom->objs);
	cube_t c;
	set_cube_zvals(c, zval, zval+height);
	bool center_tried[4] = {};

	for (unsigned n = 0; n < 25; ++n) { // make 25 attempts to place the object
		bool const use_pref(pref_orient < 4 && n < 10); // use pref orient for first 10 tries
		bool const dim((force_dim < 2) ? force_dim : (use_pref ? (pref_orient >> 1) : rgen.rand_bool())); // choose a random wall unless forced
		bool const dir(use_pref ? !(pref_orient & 1) : rgen.rand_bool()); // dir is inverted for the model, so we invert pref dir as well
		unsigned const orient(2*dim + dir);
		float center(0.0);
		if (pref_centered && !center_tried[orient]) {center = place_area.get_center_dim(!dim); center_tried[orient] = 1;} // try centered
		else {center = rgen.rand_uniform(place_area.d[!dim][0]+hwidth, place_area.d[!dim][1]-hwidth);} // random position
		c.d[ dim][ dir] = place_area.d[dim][dir];
		c.d[ dim][!dir] = c.d[dim][dir] + (dir ? -1.0 : 1.0)*depth;
		c.d[!dim][   0] = center - hwidth;
		c.d[!dim][   1] = center + hwidth;

		if (not_at_window && classify_room_wall(room, zval, dim, dir, 0) == ROOM_WALL_EXT) {
			cube_t const part(get_part_for_room(room));
			float const hspacing(get_hspacing_for_part(part, !dim)), border(get_window_h_border());
			// assume object is no larger than 2x window size and check left, right, and center positions
			if (is_val_inside_window(part, !dim, c.d[!dim][0], hspacing, border) ||
				is_val_inside_window(part, !dim, c.d[!dim][1], hspacing, border) ||
				is_val_inside_window(part, !dim, c.get_center_dim(!dim), hspacing, border)) continue;
		}
		cube_t c2(c), c3(c); // used for collision tests
		c2.d[dim][!dir] += (dir ? -1.0 : 1.0)*clearance;
		if (overlaps_other_room_obj(c2, objs_start) || interior->is_blocked_by_stairs_or_elevator(c2)) continue; // bad placement (Note: not using is_obj_placement_blocked())
		c3.d[dim][!dir] += (dir ? -1.0 : 1.0)*obj_clearance; // smaller clearance value (without player diameter)

		if (add_door_clearance) {
			if (is_cube_close_to_doorway(c3, room, 0.0, 1)) continue; // bad placement
		}
		else { // we don't need clearance for both door and object; test the object itself against the open door and the object with clearance against the closed door
			if (is_cube_close_to_doorway(c,  room, 0.0, 1)) continue; // bad placement
			if (is_cube_close_to_doorway(c3, room, 0.0, 0)) continue; // bad placement
		}
		if (!check_cube_within_part_sides(c)) continue; // handle non-cube buildings
		unsigned const flags((type == TYPE_BOX) ? (RO_FLAG_ADJ_LO << orient) : 0); // set wall edge bit for boxes (what about other dim bit if place in room corner?)
		objs.emplace_back(c, type, room_id, dim, !dir, flags, tot_light_amt, shape, color);
		set_obj_id(objs);
		if (front_clearance > 0.0) {objs.emplace_back(c2, TYPE_BLOCKER, room_id, dim, !dir, RO_FLAG_INVIS);} // add blocker cube to ensure no other object overlaps this space
		return 1; // done
	} // for n
	return 0; // failed
}
bool building_t::place_model_along_wall(unsigned model_id, room_object type, room_t const &room, float height, rand_gen_t &rgen, float zval, unsigned room_id, float tot_light_amt,
	cube_t const &place_area, unsigned objs_start, float front_clearance, unsigned pref_orient, bool pref_centered, colorRGBA const &color, bool not_at_window)
{
	if (!building_obj_model_loader.is_model_valid(model_id)) return 0; // don't have a model of this type
	vector3d const sz(building_obj_model_loader.get_model_world_space_size(model_id)); // D, W, H
	return place_obj_along_wall(type, room, height*get_window_vspace(), sz, rgen, zval, room_id, tot_light_amt,
		place_area, objs_start, front_clearance, 0, pref_orient, pref_centered, color, not_at_window);
}

float building_t::add_flooring(room_t const &room, float &zval, unsigned room_id, float tot_light_amt, unsigned flooring_type) {
	float const new_zval(zval + 0.0012*get_window_vspace());
	cube_t floor(get_walkable_room_bounds(room));
	set_cube_zvals(floor, zval, new_zval);
	interior->room_geom->objs.emplace_back(floor, TYPE_FLOORING, room_id, 0, 0, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CUBE, WHITE, flooring_type);
	return new_zval;
}

bool building_t::add_bathroom_objs(rand_gen_t rgen, room_t &room, float &zval, unsigned room_id, float tot_light_amt,
	unsigned objs_start, unsigned floor, bool is_basement, unsigned &added_bathroom_objs_mask)
{
	// Note: zval passed by reference
	float const floor_spacing(get_window_vspace()), wall_thickness(get_wall_thickness());

	if (!skylights.empty()) { // check for skylights; should we allow bathroom stalls (with ceilings?) even if there's a skylight?
		cube_t test_cube(room);
		set_cube_zvals(test_cube, zval, zval+floor_spacing);
		if (check_skylight_intersection(test_cube)) return 0;
	}
	cube_t room_bounds(get_walkable_room_bounds(room)), place_area(room_bounds);
	place_area.expand_by(-0.5*wall_thickness);
	if (min(place_area.dx(), place_area.dy()) < 0.7*floor_spacing) return 0; // room is too small (should be rare)
	bool const have_toilet(building_obj_model_loader.is_model_valid(OBJ_MODEL_TOILET)), have_sink(building_obj_model_loader.is_model_valid(OBJ_MODEL_SINK));
	vect_room_object_t &objs(interior->room_geom->objs);

	if ((have_toilet || have_sink) && is_cube()) { // bathroom with at least a toilet or sink; cube shaped parts only
		int const flooring_type(is_house ? (is_basement ? (int)FLOORING_CONCRETE : (int)FLOORING_TILE) : (int)FLOORING_MARBLE);
		if (flooring_type == FLOORING_CONCRETE && get_material().basement_floor_tex.tid == get_concrete_tid()) {} // already concrete
		else { // replace carpet/wood with marble/tile/concrete
			zval = add_flooring(room, zval, room_id, tot_light_amt, flooring_type); // move the effective floor up
		}
	}
	if (have_toilet && room.is_office) { // office bathroom
		float const room_dx(place_area.dx()), room_dy(place_area.dy());

		if (min(room_dx, room_dy) > 1.5*floor_spacing && max(room_dx, room_dy) > 2.0*floor_spacing) {
			if (divide_bathroom_into_stalls(rgen, room, zval, room_id, tot_light_amt, floor)) { // large enough, try to divide into bathroom stalls
				added_bathroom_objs_mask |= (PLACED_TOILET | PLACED_SINK);
				return 1;
			}
		}
	}
	bool placed_obj(0), placed_toilet(0);
	
	// place toilet first because it's in the corner out of the way and higher priority
	if (have_toilet) { // have a toilet model
		vector3d const sz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_TOILET)); // L, W, H
		float const height(0.35*floor_spacing), width(height*sz.y/sz.z), length(height*sz.x/sz.z); // for toilet
		unsigned const first_corner(rgen.rand() & 3);
		bool const first_dim(rgen.rand_bool());

		for (unsigned n = 0; n < 4 && !placed_toilet; ++n) { // try 4 room corners
			unsigned const corner_ix((first_corner + n)&3);
			bool const xdir(corner_ix&1), ydir(corner_ix>>1);
			point const corner(place_area.d[0][xdir], place_area.d[1][ydir], zval);
			if (!check_pt_within_part_sides(corner)) continue; // invalid corner

			for (unsigned d = 0; d < 2 && !placed_toilet; ++d) { // try both dims
				bool const dim(bool(d) ^ first_dim), dir(dim ? ydir : xdir);
				cube_t c(corner, corner);
				c.d[0][!xdir] += (xdir ? -1.0 : 1.0)*(dim ? width : length);
				c.d[1][!ydir] += (ydir ? -1.0 : 1.0)*(dim ? length : width);
				for (unsigned e = 0; e < 2; ++e) {c.d[!dim][e] += ((dim ? xdir : ydir) ? -1.5 : 1.5)*wall_thickness;} // extra padding on left and right sides
				c.z2() += height;
				cube_t c2(c); // used for placement tests
				c2.d[dim][!dir] += (dir ? -1.0 : 1.0)*0.8*length; // extra padding in front of toilet, to avoid placing other objects there (sink and tub)
				c2.expand_in_dim(!dim, 0.4*width); // more padding on the sides
				if (overlaps_other_room_obj(c2, objs_start) || is_cube_close_to_doorway(c2, room, 0.0, 1)) continue; // bad placement
				objs.emplace_back(c,  TYPE_TOILET,  room_id, dim, !dir, 0, tot_light_amt);
				objs.emplace_back(c2, TYPE_BLOCKER, room_id, 0, 0, RO_FLAG_INVIS); // add blocker cube to ensure no other object overlaps this space
				placed_obj = placed_toilet = 1; // done
				added_bathroom_objs_mask  |= PLACED_TOILET;

				// try to place a roll of toilet paper on the adjacent wall
				bool const tp_dir(dim ? xdir : ydir);
				float const length(0.18*height), wall_pos(c.get_center_dim(dim)), far_edge_pos(wall_pos + (dir ? -1.0 : 1.0)*0.5*length);
				cube_t const part(get_part_for_room(room));

				// if this wall has windows and bathroom has multiple exterior walls (which means it has non-glass block windows), don't place a TP roll
				if (is_basement || !has_windows() || classify_room_wall(room, zval, !dim, tp_dir, 0) != ROOM_WALL_EXT ||
					!is_val_inside_window(part, dim, far_edge_pos, get_hspacing_for_part(part, dim), get_window_h_border()) || count_ext_walls_for_room(room, zval) <= 1)
				{
					add_tp_roll(room_bounds, room_id, tot_light_amt, !dim, tp_dir, length, (c.z1() + 0.7*height), wall_pos);
				}
			} // for d
		} // for n
		if (!placed_toilet) { // if the toilet can't be placed in a corner, allow it to be placed anywhere; needed for small offices
			placed_toilet = place_model_along_wall(OBJ_MODEL_TOILET, TYPE_TOILET, room, 0.35, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 0.8);
			placed_obj   |= placed_toilet;
			added_bathroom_objs_mask |= PLACED_TOILET;

			if (placed_toilet) { // if toilet was placed, try to place a roll of toilet paper on the same wall as the toilet
				room_object_t const &toilet(objs.back()); // okay if this is the blocker
				
				// Note: not calling is_val_inside_window() here because I don't have a test case for that and it may not even be possible to get here when the toilet is next to a window
				if (is_basement || !has_windows() || classify_room_wall(room, zval, toilet.dim, !toilet.dir, 0) != ROOM_WALL_EXT) { // check for possible windows
					bool place_dir(rgen.rand_bool()); // pick a random starting side

					for (unsigned d = 0; d < 2; ++d) {
						float const length(0.18*height), wall_pos(toilet.d[!toilet.dim][place_dir] + (place_dir ? 1.0 : -1.0)*0.5*width);
						if (add_tp_roll(room_bounds, room_id, tot_light_amt, toilet.dim, !toilet.dir, length, (toilet.z1() + 0.7*height), wall_pos, 1)) break; // check_valid=1
						place_dir ^= 1; // try the other dir
					} // for d
				}
			}
		}
	}
	if (is_house && !is_basement && (floor > 0 || rgen.rand_bool())) { // try to add a shower; 50% chance if on first floor; not in basements (due to drawing artifacts)
		float const shower_height(0.8*floor_spacing);
		float shower_dx(rgen.rand_uniform(0.4, 0.5)*floor_spacing), shower_dy(rgen.rand_uniform(0.4, 0.5)*floor_spacing);
		bool hdim(shower_dx < shower_dy); // larger dim, ust match handle/door drawing code
		unsigned const first_corner(rgen.rand() & 3);
		//cube_t const part(get_part_for_room(room));
		bool placed_shower(0), is_ext_wall[2][2] = {0};
		
		if (!is_basement && has_windows()) { // precompute which walls are exterior, {dim}x{dir}; basement walls are not considered exterior because there are no windows
			for (unsigned d = 0; d < 4; ++d) {is_ext_wall[d>>1][d&1] = (classify_room_wall(room, zval, (d>>1), (d&1), 0) == ROOM_WALL_EXT);}
		}
		for (unsigned ar = 0; ar < 2; ++ar) { // try both aspect ratios/door sides
			for (unsigned n = 0; n < 4; ++n) { // try 4 room corners
				unsigned const corner_ix((first_corner + n)&3);
				bool const xdir(corner_ix&1), ydir(corner_ix>>1), dirs[2] = {xdir, ydir};
				point const corner(room_bounds.d[0][xdir], room_bounds.d[1][ydir], zval); // flush against the wall
				cube_t c(corner, corner);
				c.d[0][!xdir] += (xdir ? -1.0 : 1.0)*shower_dx;
				c.d[1][!ydir] += (ydir ? -1.0 : 1.0)*shower_dy;
				c.z2() += shower_height; // set height
				bool is_bad(0);

				for (unsigned d = 0; d < 2; ++d) { // check for window intersection
					// Update: exterior walls aren't drawn in the correct order for glass alpha blend, so skip any exterior walls
					if (is_ext_wall[!d][dirs[!d]] /*&& is_val_inside_window(part, d, c.d[d][!dirs[d]], get_hspacing_for_part(part, d), get_window_h_border())*/) {is_bad = 1; break;}
				}
				if (is_bad) continue;
				cube_t c2(c); // used for placement tests; extend out by door width on the side that opens, and a small amount on the other side
				c2.d[0][!xdir] += (xdir ? -1.0 : 1.0)*((!hdim) ? 1.1*shower_dy : 0.2*shower_dx);
				c2.d[1][!ydir] += (ydir ? -1.0 : 1.0)*(  hdim  ? 1.1*shower_dx : 0.2*shower_dy);
				if (overlaps_other_room_obj(c2, objs_start) || is_cube_close_to_doorway(c2, room, 0.0, 1)) continue; // bad placement
				objs.emplace_back(c,  TYPE_SHOWER,  room_id, xdir, ydir, 0, tot_light_amt);
				set_obj_id(objs); // selects tile texture/color
				objs.emplace_back(c2, TYPE_BLOCKER, room_id, 0, 0, RO_FLAG_INVIS); // add blocker cube to ensure no other object overlaps this space
				placed_obj = placed_shower = 1;
				added_bathroom_objs_mask  |= PLACED_SHOWER;
				break; // done
			} // for n
			if (placed_shower) break; // done
			swap(shower_dx, shower_dy); // try the other aspect ratio
			hdim ^= 1;
		} // for ar
	}
	if (is_house && (!is_basement || rgen.rand_bool())) { // 50% of the time if in the basement
		// place a tub, but not in office buildings; placed before the sink because it's the largest and the most limited in valid locations
		cube_t place_area_tub(room_bounds);
		place_area_tub.expand_by(-get_trim_thickness()); // just enough to prevent z-fighting and intersecting the wall trim
		
		if (place_model_along_wall(OBJ_MODEL_TUB, TYPE_TUB, room, 0.2, rgen, zval, room_id, tot_light_amt, place_area_tub, objs_start, 0.4)) {
			placed_obj = 1;
			added_bathroom_objs_mask |= PLACED_TUB;
		}
	}
	unsigned const sink_obj_ix(objs.size());

	if (place_model_along_wall(OBJ_MODEL_SINK, TYPE_SINK, room, 0.45, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 0.6)) {
		placed_obj = 1;
		added_bathroom_objs_mask |= PLACED_SINK;
		assert(sink_obj_ix < objs.size());
		room_object_t const &sink(objs[sink_obj_ix]); // sink, not blocker
		
		if (point_in_water_area(sink.get_cube_center())) {} // no medicine cabinet, because the reflection system doesn't support both a mirror and water reflection
		else if (is_basement || classify_room_wall(room, zval, sink.dim, !sink.dir, 0) != ROOM_WALL_EXT) { // interior wall only
			// add a mirror/medicine cabinet above the sink; could later make into medicine cabinet
			cube_t mirror(sink); // start with the sink left and right position
			mirror.expand_in_dim(!sink.dim, 0.1*mirror.get_sz_dim(!sink.dim)); // make slightly wider
			set_cube_zvals(mirror, sink.z2(), sink.z2()+0.3*floor_spacing);
			mirror.d[sink.dim][!sink.dir] = room_bounds.d[sink.dim][!sink.dir];
			mirror.d[sink.dim][ sink.dir] = mirror.d[sink.dim][!sink.dir] + (sink.dir ? 1.0 : -1.0)*1.0*wall_thickness; // thickness

			if (!overlaps_other_room_obj(mirror, objs_start, 0, &sink_obj_ix)) { // check_all=0; skip sink + blocker
				// this mirror is actually 3D, so we enable collision detection; treat as a house even if it's in an office building
				unsigned flags(RO_FLAG_IS_HOUSE);
				if (count_ext_walls_for_room(room, mirror.z1()) == 1) {flags |= RO_FLAG_INTERIOR;} // flag as interior if windows are opaque glass blocks
				objs.emplace_back(mirror, TYPE_MIRROR, room_id, sink.dim, sink.dir, flags, tot_light_amt);
				set_obj_id(objs); // for crack texture selection/orient
				room.has_mirror = 1;
			}
		}
	}
	return placed_obj;
}

bool building_t::add_tp_roll(cube_t const &room, unsigned room_id, float tot_light_amt, bool dim, bool dir, float length, float zval, float wall_pos, bool check_valid_pos) {
	float const diameter(length);
	cube_t tp;
	set_cube_zvals(tp, zval, (zval + diameter));
	set_wall_width(tp, wall_pos, 0.5*length, !dim); // set length
	tp.d[dim][ dir] = room.d[dim][dir]; // against the wall
	tp.d[dim][!dir] = tp  .d[dim][dir] + (dir ? -1.0 : 1.0)*diameter; // set the diameter
	// Note: not checked against other bathroom objects because the toilet is placed first
	if (check_valid_pos && (!room.contains_cube(tp) || is_obj_placement_blocked(tp, room, 1))) return 0;
	interior->room_geom->objs.emplace_back(tp, TYPE_TPROLL, room_id, dim, dir, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN, WHITE);
	set_obj_id(interior->room_geom->objs);
	return 1;
}

void add_hallway_sign(vect_room_object_t &objs, cube_t const &sign, string const &text, unsigned room_id, bool dim, bool dir) {
	// Note: room_id is for the sign's room, not the hallway, though this doesn't seem to be a problem
	float const sign_light_amt(1.0); // assume well lit since it's in the hallway, not in the room that the sign is attached to
	objs.emplace_back(sign, TYPE_SIGN, room_id, dim, dir, RO_FLAG_NOCOLL, sign_light_amt, SHAPE_CUBE, DK_BLUE); // technically should use hallway room_id
	objs.back().obj_id = register_sign_text(text);
}

bool building_t::divide_bathroom_into_stalls(rand_gen_t &rgen, room_t &room, float zval, unsigned room_id, float tot_light_amt, unsigned floor) {
	// Note: assumes no prior placed objects
	bool const use_sink_model(0 && building_obj_model_loader.is_model_valid(OBJ_MODEL_SINK)); // not using sink models
	float const floor_spacing(get_window_vspace()), wall_thickness(get_wall_thickness());
	vector3d const tsz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_TOILET)); // L, W, H
	float const theight(0.35*floor_spacing), twidth(theight*tsz.y/tsz.z), tlength(theight*tsz.x/tsz.z), stall_depth(2.2*tlength);
	float sheight(0), swidth(0), slength(0), uheight(0), uwidth(0), ulength(0);

	if (use_sink_model) {
		vector3d const ssz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_SINK)); // L, W, H
		sheight = 0.45*floor_spacing; swidth = sheight*ssz.y/ssz.z; slength = sheight*ssz.x/ssz.z;
	}
	else {
		sheight = 0.36*floor_spacing; swidth = 0.3*floor_spacing; slength = 0.32*floor_spacing;
		//slength = (has_parking_garage ? (tlength + 2.0*wall_thickness) : 0.32*floor_spacing); // align sink drain to toilets for parking garage pipes?
	}
	float stall_width(2.0*twidth), sink_spacing(1.75*swidth);
	bool br_dim(room.dy() < room.dx()), sink_side(0), sink_side_set(0); // br_dim is the smaller dim
	cube_t place_area(room), br_door;
	place_area.expand_by(-0.5*wall_thickness);

	// determine men's room vs. women's room
	point const part_center(get_part_for_room(room).get_cube_center()), room_center(room.get_cube_center());
	bool mens_room((part_center.x < room_center.x) ^ (part_center.y < room_center.y)), has_second_bathroom(0);

	// if there are two bathrooms (one on each side of the building), assign a gender to each side; if only one, alternate gender per floor
	for (auto r = interior->rooms.begin(); r != interior->rooms.end(); ++r) {
		if (r->part_id != room.part_id || &(*r) == &room) continue; // different part or same room
		if (is_room_office_bathroom(*r, zval, floor)) {has_second_bathroom = 1; break;}
	}
	if (!has_second_bathroom) {mens_room ^= (floor & 1);}
	bool const add_urinals(mens_room && building_obj_model_loader.is_model_valid(OBJ_MODEL_URINAL));

	if (add_urinals) { // use urinal model
		vector3d const usz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_URINAL)); // L, W, H
		uheight = 0.4*floor_spacing; uwidth = uheight*usz.y/usz.z; ulength = uheight*usz.x/usz.z;
	}
	for (unsigned d = 0; d < 2 && !sink_side_set; ++d) {
		for (unsigned side = 0; side < 2 && !sink_side_set; ++side) {
			cube_t c(room);
			set_cube_zvals(c, zval, zval+wall_thickness); // reduce to a small z strip for this floor to avoid picking up doors on floors above or below
			c.d[!br_dim][!side] = c.d[!br_dim][side] + (side ? -1.0 : 1.0)*wall_thickness; // shrink to near zero area in this dim

			for (auto i = interior->door_stacks.begin(); i != interior->door_stacks.end(); ++i) {
				if ((i->dy() < i->dx()) == br_dim) continue; // door in wrong dim
				if (!is_cube_close_to_door(c, 0.0, 0, *i, 2)) continue; // check both dirs
				sink_side = side; sink_side_set = 1;
				place_area.d[!br_dim][side] += (sink_side ? -1.0 : 1.0)*(i->get_sz_dim(br_dim) - 0.25*swidth); // add sink clearance for the door to close
				br_door = *i;
				break; // sinks are on the side closest to the door
			}
		} // for side
		if (d == 0 && !sink_side_set) {br_dim ^= 1;} // door not found on long dim - R90 and try short dim
	} // for d
	assert(sink_side_set);
	float const room_len(place_area.get_sz_dim(!br_dim)), room_width(place_area.get_sz_dim(br_dim));
	float const sinks_len(0.4*room_len), stalls_len(room_len - sinks_len), req_depth(2.0f*max(stall_depth, slength));
	if (room_width < req_depth) return 0;
	if (sinks_len < 2.0*sink_spacing) {sink_spacing *= 0.8;} // reduce sink spacing a bit to try and fit at least two
	unsigned const num_stalls(std::floor(stalls_len/stall_width)), num_sinks(std::floor(sinks_len/sink_spacing));
	if (num_stalls < 2 || num_sinks < 1) return 0; // not enough space for 2 stalls and a sink
	stall_width  = stalls_len/num_stalls; // reclaculate to fill the gaps
	sink_spacing = sinks_len/num_sinks;
	bool const two_rows(room_width > 1.5*req_depth), skip_stalls_side(room_id & 1); // put stalls on a side consistent across floors
	float const sink_side_sign(sink_side ? 1.0 : -1.0), stall_step(sink_side_sign*stall_width), sink_step(-sink_side_sign*sink_spacing);
	float const floor_thickness(get_floor_thickness());
	unsigned const NUM_STALL_COLORS = 4;
	colorRGBA const stall_colors[NUM_STALL_COLORS] = {colorRGBA(0.75, 1.0, 0.9, 1.0), colorRGBA(0.7, 0.8, 1.0), WHITE, DK_GRAY}; // blue-green, light blue
	colorRGBA const stall_color(stall_colors[interior->doors.size() % NUM_STALL_COLORS]); // random, but constant for each building
	vect_room_object_t &objs(interior->room_geom->objs);

	for (unsigned dir = 0; dir < 2; ++dir) { // each side of the wall
		if (!two_rows && dir == (unsigned)skip_stalls_side) continue; // no stalls/sinks on this side
		// add stalls
		float const dir_sign(dir ? -1.0 : 1.0), wall_pos(place_area.d[br_dim][dir]), stall_from_wall(wall_pos + dir_sign*(0.5*tlength + wall_thickness));
		float stall_pos(place_area.d[!br_dim][!sink_side] + 0.5*stall_step);

		for (unsigned n = 0; n < num_stalls; ++n, stall_pos += stall_step) {
			point center(stall_from_wall, stall_pos, zval);
			if (br_dim) {swap(center.x, center.y);} // R90 about z
			cube_t toilet(center, center), stall(toilet);
			toilet.expand_in_dim( br_dim, 0.5*tlength);
			toilet.expand_in_dim(!br_dim, 0.5*twidth);
			toilet.z2() += theight;
			stall.z2() = stall.z1() + floor_spacing - floor_thickness; // set stall height to room height
			stall.expand_in_dim(!br_dim, 0.5*stall_width);
			stall.d[br_dim][ dir] = wall_pos; // + wall_thickness?
			stall.d[br_dim][!dir] = wall_pos + dir_sign*stall_depth;
			if (interior->is_cube_close_to_doorway(stall, room, 0.0, 1)) continue; // skip if close to a door (for rooms with doors at both ends); inc_open=1
			if (!check_cube_within_part_sides(stall)) continue; // outside the building
			bool const is_open(rgen.rand_bool()); // 50% chance of stall door being open
			objs.emplace_back(toilet, TYPE_TOILET, room_id, br_dim, !dir, 0, tot_light_amt);
			objs.emplace_back(stall,  TYPE_STALL,  room_id, br_dim,  dir, (is_open ? RO_FLAG_OPEN : 0), tot_light_amt, SHAPE_CUBE, stall_color);
			float const tp_length(0.18*theight), wall_pos(toilet.get_center_dim(br_dim));
			cube_t stall_inner(stall);
			stall_inner.expand_in_dim(!br_dim, -0.0125*stall.dz()); // subtract off stall wall thickness
			add_tp_roll(stall_inner, room_id, tot_light_amt, !br_dim, dir, tp_length, (zval + 0.7*theight), wall_pos);
		} // for n
		if (add_urinals && dir == (unsigned)skip_stalls_side) continue; // no urinals and sinks are each on one side
		// add sinks
		float const sink_start(place_area.d[!br_dim][sink_side] + 0.5f*sink_step);
		float const sink_from_wall(wall_pos + dir_sign*(0.5f*slength + (use_sink_model ? wall_thickness : 0.0f)));
		float sink_pos(sink_start);
		bool hit_mirror_end(0);
		unsigned last_sink_ix(0);
		cube_t sinks_bcube;

		for (unsigned n = 0; n < num_sinks; ++n, sink_pos += sink_step) {
			point center(sink_from_wall, sink_pos, zval);
			if (br_dim) {swap(center.x, center.y);} // R90 about z
			cube_t sink(center, center);
			sink.expand_in_dim(br_dim, 0.5*slength);
			sink.z2() += sheight;
			if (interior->is_cube_close_to_doorway(sink, room, 0.0, 1)) continue; // skip if close to a door, inc_open=1, pre expand
			sink.expand_in_dim(!br_dim, 0.5*(use_sink_model ? swidth : fabs(sink_step))); // tile exactly with the adjacent sink
			if (interior->is_cube_close_to_doorway(sink, room, 0.0, 0)) continue; // skip if close to a door
			if (!check_cube_within_part_sides(sink)) continue; // outside the building
			if (use_sink_model) {objs.emplace_back(sink, TYPE_SINK,   room_id, br_dim, !dir, 0, tot_light_amt);} // sink 3D model
			else                {objs.emplace_back(sink, TYPE_BRSINK, room_id, br_dim, !dir, 0, tot_light_amt);} // flat basin sink
			// if we started the mirror, but we have a gap with no sink (blocked by a door, etc.), then end the mirror
			hit_mirror_end |= (n > last_sink_ix+1 && !sinks_bcube.is_all_zeros());
			if (!hit_mirror_end) {sinks_bcube.assign_or_union_with_cube(sink);}
			last_sink_ix = n;
		} // for n
		if (add_urinals) { // add urinals opposite the sinks, using same spacing as sinks
			float const u_wall(place_area.d[br_dim][!dir]), u_from_wall(u_wall - dir_sign*(0.5*ulength + 0.01*wall_thickness));
			float u_pos(sink_start);
			cube_t sep_wall;
			set_cube_zvals(sep_wall, zval+0.15*uheight, zval+1.25*uheight);
			sep_wall.d[br_dim][!dir] = u_wall;
			sep_wall.d[br_dim][ dir] = u_wall - dir_sign*0.25*floor_spacing;

			for (unsigned n = 0; n < num_sinks; ++n, u_pos += sink_step) {
				set_wall_width(sep_wall, (u_pos - 0.5*sink_step), 0.2*wall_thickness, !br_dim);
				point center(u_from_wall, u_pos, (zval + 0.2*uheight));
				if (br_dim) {swap(center.x, center.y);} // R90 about z
				cube_t urinal(center, center);
				urinal.expand_in_dim( br_dim, 0.5*ulength);
				urinal.expand_in_dim(!br_dim, 0.5*uwidth);
				urinal.z2() += uheight;
				if (interior->is_cube_close_to_doorway(urinal, room, 0.0, 1)) continue; // skip if close to a door
				if (!check_cube_within_part_sides(urinal)) continue; // outside the building
				objs.emplace_back(sep_wall, TYPE_STALL,  room_id, br_dim, !dir, 0, tot_light_amt, SHAPE_SHORT, stall_color);
				objs.emplace_back(urinal,   TYPE_URINAL, room_id, br_dim,  dir, 0, tot_light_amt);
			} // for n
			if (!two_rows) { // skip first wall if adjacent to a stall
				set_wall_width(sep_wall, (u_pos - 0.5*sink_step), 0.2*wall_thickness, !br_dim);
				objs.emplace_back(sep_wall, TYPE_STALL, room_id, br_dim, !dir, 0, tot_light_amt, SHAPE_SHORT, stall_color);
			}
		}
		if (!sinks_bcube.is_all_zeros()) { // add a long mirror above the sink
			if (!ENABLE_MIRROR_REFLECTIONS || dir != (unsigned)skip_stalls_side) { // don't add mirrors to both sides if reflections are enabled
				cube_t mirror(sinks_bcube);
				mirror.expand_in_dim(!br_dim, -0.25*wall_thickness); // slightly smaller
				mirror.d[br_dim][ dir] = wall_pos;
				mirror.d[br_dim][!dir] = wall_pos + dir_sign*0.1*wall_thickness;
				mirror.z1() = sinks_bcube.z2() + 0.25*floor_thickness;
				mirror.z2() = zval + 0.9*floor_spacing - floor_thickness;

				if (mirror.is_strictly_normalized()) {
					objs.emplace_back(mirror, TYPE_MIRROR, room_id, br_dim, !dir, RO_FLAG_NOCOLL, tot_light_amt);
					set_obj_id(objs); // for crack texture selection/orient
					room.has_mirror = 1;
				}
			}
		}
	} // for dir
	// add a sign outside the bathroom door
	//add_door_sign((mens_room ? "Men" : "Women"), room, zval, room_id, tot_light_amt); // equivalent, but below is more efficient
	bool const shift_dir(room_center[br_dim] < part_center[br_dim]); // put the sign toward the outside of the building because there's more space and more light
	float const door_width(br_door.get_sz_dim(br_dim));
	cube_t sign(br_door);
	set_cube_zvals(sign, zval+0.50*floor_spacing, zval+0.55*floor_spacing);
	sign.translate_dim( br_dim, (shift_dir ? -1.0 : 1.0)*0.8*door_width);
	sign.expand_in_dim( br_dim, -(mens_room ? 0.36 : 0.30)*door_width); // shrink a bit
	sign.translate_dim(!br_dim, sink_side_sign*0.5*wall_thickness); // move to outside wall
	sign.d[!br_dim][sink_side] += sink_side_sign*0.1*wall_thickness; // make nonzero area
	add_hallway_sign(objs, sign, (mens_room ? "Men" : "Women"), room_id, !br_dim, sink_side);
	return 1;
}

void building_t::add_door_sign(string const &text, room_t const &room, float zval, unsigned room_id, float tot_light_amt) {
	float const floor_spacing(get_window_vspace()), wall_thickness(get_wall_thickness());
	point const part_center(get_part_for_room(room).get_cube_center()), room_center(room.get_cube_center());
	cube_t c(room);
	set_cube_zvals(c, zval, zval+wall_thickness); // reduce to a small z strip for this floor to avoid picking up doors on floors above or below

	for (auto i = interior->door_stacks.begin(); i != interior->door_stacks.end(); ++i) {
		if (!is_cube_close_to_door(c, 0.0, 0, *i, 2)) continue; // check both dirs; should we check that the room on the other side of the door is a hallway?
		// put the sign toward the outside of the building because there's more space and more light
		bool const side(room_center[i->dim] < i->get_center_dim(i->dim)), shift_dir(room_center[!i->dim] < part_center[!i->dim]);
		float const door_width(i->get_width()), side_sign(side ? 1.0 : -1.0);
		cube_t sign(*i);
		set_cube_zvals(sign, zval+0.50*floor_spacing, zval+0.55*floor_spacing);
		sign.translate_dim(!i->dim, (shift_dir ? -1.0 : 1.0)*0.8*door_width);
		sign.expand_in_dim(!i->dim, -(0.45 - 0.03*min((unsigned)text.size(), 6U))*door_width); // shrink a bit
		sign.translate_dim( i->dim, side_sign*0.5*wall_thickness); // move to outside wall
		sign.d[i->dim][side] += side_sign*0.1*wall_thickness; // make nonzero area
		cube_t test_cube(sign);
		test_cube.translate_dim(i->dim, side_sign*0.1*wall_thickness); // move out in front of the current wall to avoid colliding with it (in case of T-junction)
		if (has_bcube_int(test_cube, interior->walls[!i->dim])) continue; // check for intersections with orthogonal walls; needed for inside corner offices
		add_hallway_sign(interior->room_geom->objs, sign, text, room_id, i->dim, side);
	} // for i
}
void building_t::add_office_door_sign(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt) {
	string const name(gen_random_full_name(rgen));
	add_door_sign(name, room, zval, room_id, tot_light_amt); // will cache the name; maybe it shouldn't?
}

void add_door_if_blocker(cube_t const &door, cube_t const &room, bool inc_open, bool dir, bool hinge_side, vect_cube_t &blockers) {
	bool const dim(door.dy() < door.dx()), edir(dim ^ dir ^ hinge_side ^ 1);
	float const width(door.get_sz_dim(!dim));
	cube_t door_exp(door);
	door_exp.expand_in_dim(dim, width);
	if (!door_exp.intersects(room)) return; // check against room before expanding along wall to exclude doors in adjacent rooms
	door_exp.expand_in_dim(!dim, width*0.25); // min expand value
	if (inc_open) {door_exp.d[!dim][edir] += (edir ? 1.0 : -1.0)*0.75*width;} // expand the remainder of the door width in this dir
	blockers.push_back(door_exp);
}
int building_t::gather_room_placement_blockers(cube_t const &room, unsigned objs_start, vect_cube_t &blockers, bool inc_open_doors, bool ignore_chairs) const {
	assert(has_room_geom());
	vect_room_object_t &objs(interior->room_geom->objs);
	assert(objs_start <= objs.size());
	blockers.clear();
	int table_blocker_ix(-1);

	for (auto i = objs.begin()+objs_start; i != objs.end(); ++i) {
		if (ignore_chairs && i->type == TYPE_CHAIR) continue;
		
		if (!i->no_coll() && i->intersects(room)) {
			if (i->type == TYPE_TABLE) {table_blocker_ix = int(blockers.size());} // track which blocker is the table, for use with kitchen counters
			blockers.push_back(*i);
		}
	}
	for (auto i = doors.begin(); i != doors.end(); ++i) {add_door_if_blocker(i->get_bcube(), room, 0, 0, 0, blockers);} // exterior doors, inc_open=0

	for (auto i = interior->door_stacks.begin(); i != interior->door_stacks.end(); ++i) { // interior doors
		add_door_if_blocker(*i, room, door_opens_inward(*i, room), i->open_dir, i->hinge_side, blockers);
	}
	float const doorway_width(get_doorway_width());

	for (auto s = interior->stairwells.begin(); s != interior->stairwells.end(); ++s) {
		cube_t tc(*s);
		// expand only in stairs entrance dim for the first floor (could do the opposite for top floor)
		bool const first_floor(room.z1() <= s->z1() + get_floor_thickness()); // for these stairs, not for the building
		if (first_floor) {tc.d[s->dim][!s->dir] += (s->dir ? -1.0 : 1.0);}
		else {tc.expand_in_dim(s->dim, doorway_width);} // add extra space at both ends of stairs
		if (tc.intersects(bcube)) {blockers.push_back(tc);}
	}
	for (auto e = interior->elevators.begin(); e != interior->elevators.end(); ++e) {
		cube_t tc(*e);
		tc.d[e->dim][e->dir] += doorway_width*(e->dir ? 1.0 : -1.0); // add extra space in front of the elevator
		if (tc.intersects(bcube)) {blockers.push_back(tc);}
	}
	return table_blocker_ix;
}

bool building_t::add_kitchen_objs(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, bool allow_adj_ext_door) {
	// Note: table and chairs have already been placed
	if (room.is_hallway || room.is_sec_bldg || room.is_office) return 0; // these can't be kitchens
	if (!is_house && rgen.rand_bool()) return 0; // some office buildings have kitchens, allow it half the time
	// if it has an external door then reject the room half the time; most houses don't have a front door to the kitchen
	if (is_room_adjacent_to_ext_door(room, 1) && (!allow_adj_ext_door || rgen.rand_bool())) return 0; // front_door_only=1
	float const wall_thickness(get_wall_thickness());
	cube_t room_bounds(get_walkable_room_bounds(room)), place_area(room_bounds);
	place_area.expand_by(-0.25*wall_thickness); // common spacing to wall for appliances
	vect_room_object_t &objs(interior->room_geom->objs);
	bool placed_obj(0);
	placed_obj |= place_model_along_wall(OBJ_MODEL_FRIDGE, TYPE_FRIDGE, room, 0.75, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 1.2, 4, 0, WHITE, 1); // not at window
	
	if (is_house) { // try to place a stove
		unsigned const stove_ix(objs.size()); // can't use objs.back() because there's a blocker
		
		if (place_model_along_wall(OBJ_MODEL_STOVE, TYPE_STOVE, room, 0.46, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 1.0)) {
			assert(stove_ix < objs.size());

			if (building_obj_model_loader.is_model_valid(OBJ_MODEL_HOOD)) { // add hood above the stove
				room_object_t const &stove(objs[stove_ix]);
				vector3d const sz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_HOOD)); // D, W, H
				float const width(stove.get_sz_dim(!stove.dim)), height(width*sz.z/sz.y), depth(width*sz.x/sz.y); // scale to the width of the stove
				float const ceiling_z(zval + get_floor_ceil_gap()), z_top(ceiling_z + get_fc_thickness()); // shift up a bit because it's too low
				cube_t hood(stove);
				set_cube_zvals(hood, z_top-height, z_top);
				hood.d[stove.dim][stove.dir] = stove.d[stove.dim][!stove.dir] + (stove.dir ? 1.0 : -1.0)*depth;
				objs.emplace_back(hood, TYPE_HOOD, room_id, stove.dim, stove.dir, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CUBE, LT_GRAY);
				
				if (has_attic()) { // add rooftop vent above the hood; should be close to the edge of the house in many cases
					float const attic_floor_zval(get_attic_part().z2()), vent_radius(0.075*(width + depth)); // kitchen can be in a part below the one with the attic
					point const vent_bot_center(hood.xc(), hood.yc(), attic_floor_zval);
					add_attic_roof_vent(vent_bot_center, vent_radius, room_id, 1.0); // light_amt=1.0; room_id is for the kitchen because there's no attic room
				}
			}
			if (!rgen.rand_bool()) { // maybe add a pan on one of the stove burners
				room_object_t const &stove(objs[stove_ix]);
				float const stove_height(stove.dz()), delta_z(0.018*stove_height);
				float const pan_radius(rgen.rand_uniform(0.075, 0.09)*stove_height), pan_height(rgen.rand_uniform(0.035, 0.045)*stove_height);
				point locs[4];
				get_stove_burner_locs(stove, locs);
				unsigned const burner_ix(rgen.rand() & 3); // 0-3
				point &loc(locs[burner_ix]);
				loc.z += delta_z;
				cube_t burner(loc, loc);
				burner.expand_by_xy(pan_radius);
				burner.z2() += pan_height;
				objs.emplace_back(burner, TYPE_PAN, room_id, stove.dim, stove.dir, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN, GRAY_BLACK);
			}
			placed_obj = 1;
		}
	}	
	if (is_house && placed_obj) { // if we have at least a fridge or stove, try to add countertops
		float const vspace(get_window_vspace()), height(0.345*vspace), depth(0.74*height), min_hwidth(0.6*height), floor_thickness(get_floor_thickness());
		float const min_clearance(get_min_front_clearance_inc_people()), front_clearance(max(0.6f*height, min_clearance));
		cube_t cabinet_area(room_bounds);
		cabinet_area.expand_by(-0.05*wall_thickness); // smaller gap than place_area; this is needed to prevent z-fighting with exterior walls
		if (min(cabinet_area.dx(), cabinet_area.dy()) < 4.0*min_hwidth) return placed_obj; // no space for cabinets, room is too small
		unsigned const counters_start(objs.size());
		cube_t c;
		set_cube_zvals(c, zval, zval+height);
		set_cube_zvals(cabinet_area, zval, (zval + vspace - floor_thickness));
		static vect_cube_t blockers;
		int const table_blocker_ix(gather_room_placement_blockers(cabinet_area, objs_start, blockers, 1, 1)); // inc_open_doors=1, ignore_chairs=1
		bool const have_toaster(building_obj_model_loader.is_model_valid(OBJ_MODEL_TOASTER));
		vector3d const toaster_sz(have_toaster ? building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_TOASTER) : zero_vector); // L, D, H
		bool is_sink(1), placed_mwave(0), placed_toaster(0);
		cube_t mwave, toaster;

		for (unsigned n = 0; n < 50; ++n) { // 50 attempts
			bool const dim(rgen.rand_bool()), dir(rgen.rand_bool()); // choose a random wall
			bool const is_ext_wall(classify_room_wall(room, zval, dim, dir, 0) == ROOM_WALL_EXT); // assumes not in basement
			// only consider exterior walls in the first 20 attempts to prioritize these so that we don't have splits visible through windows; also places kitchen sinks near windows
			if (n < 20 && !is_ext_wall) continue;
			float const center(rgen.rand_uniform(cabinet_area.d[!dim][0]+min_hwidth, cabinet_area.d[!dim][1]-min_hwidth)); // random position
			float const dir_sign(dir ? -1.0 : 1.0), wall_pos(cabinet_area.d[dim][dir]), front_pos(wall_pos + dir_sign*depth);
			c.d[ dim][ dir] = wall_pos;
			c.d[ dim][!dir] = front_pos + dir_sign*front_clearance;
			c.d[!dim][   0] = center - min_hwidth;
			c.d[!dim][   1] = center + min_hwidth;
			cube_t c_min(c); // min runlength - used for collision tests
			for (unsigned e = 0; e < 2; ++e) {c.d[!dim][e] = cabinet_area.d[!dim][e];} // start at full room width
			bool bad_place(0);

			for (auto i = blockers.begin(); i != blockers.end(); ++i) {
				cube_t b(*i); // expand tables by an extra clearance to allow the player to fit in the diagonal gap between the table and the counter
				if (int(i - blockers.begin()) == table_blocker_ix) {b.expand_in_dim(!dim, min_clearance);}
				if (!b.intersects(c)) continue; // optimization - no cube interaction
				if (b.intersects(c_min)) {bad_place = 1; break;}
				if (b.d[!dim][1] < c_min.d[!dim][0]) {max_eq(c.d[!dim][0], b.d[!dim][1]);} // clip on lo side
				if (b.d[!dim][0] > c_min.d[!dim][1]) {min_eq(c.d[!dim][1], b.d[!dim][0]);} // clip on hi side
			} // for i
			if (bad_place) continue;
			assert(c.contains_cube(c_min));
			c.d[dim][!dir] = front_pos; // remove front clearance
			bool const add_backsplash(!is_ext_wall); // only add to interior walls to avoid windows; assuming not in basement

			for (auto i = objs.begin()+counters_start; i != objs.end(); ++i) { // find adjacencies to previously placed counters and flag to avoid placing doors
				if (i->dim == dim) continue; // not perpendicular
				if (i->d[!i->dim][dir] != wall_pos) continue; // not against the wall on this side
				if (i->d[i->dim][i->dir] != c.d[!dim][0] && i->d[i->dim][i->dir] != c.d[!dim][1]) continue; // not adjacent
				i->flags |= (dir ? RO_FLAG_ADJ_HI : RO_FLAG_ADJ_LO);
				if (add_backsplash) {i->flags |= RO_FLAG_HAS_EXTRA;}
			}
			unsigned const cabinet_id(objs.size());
			objs.emplace_back(c, (is_sink ? TYPE_KSINK : TYPE_COUNTER), room_id, dim, !dir, 0, tot_light_amt);
			set_obj_id(objs);
			
			if (add_backsplash) {
				objs.back().flags |= (RO_FLAG_ADJ_BOT | RO_FLAG_HAS_EXTRA); // flag back as having a back backsplash
				cube_t bs(c);
				bs.z1()  = c.z2();
				bs.z2() += 0.33*c.dz();
				bs.d[dim][!dir] -= (dir ? -1.0 : 1.0)*0.99*depth; // matches building_room_geom_t::add_counter()
				objs.emplace_back(bs, TYPE_BLOCKER, room_id, dim, !dir, RO_FLAG_INVIS); // add blocker to avoid placing light switches here
			}
			// add upper cabinets
			cube_t c2(c);
			set_cube_zvals(c2, (zval + 0.65*vspace), cabinet_area.z2()); // up to the ceiling

			if (is_ext_wall) { // possibly against a window
				max_eq(c2.z1(), (c2.z2() - vspace*get_window_v_border() + 0.5f*floor_thickness)); // increase bottom of cabinet to the top of the window
			}
			if (c2.dz() > 0.1*vspace && !has_bcube_int_no_adj(c2, blockers)) { // add if it's not too short and not blocked
				objs.emplace_back(c2, TYPE_CABINET, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt); // no collision detection
				set_obj_id(objs);
			}
			blockers.push_back(c); // add to blockers so that later counters don't intersect this one

			// place a microwave on a counter 50% of the time
			if (!is_sink && !placed_mwave && c.get_sz_dim(!dim) > 0.5*vspace && rgen.rand_bool()) {
				float const mheight(rgen.rand_uniform(1.0, 1.2)*0.14*vspace), mwidth(1.7*mheight), mdepth(1.2*mheight); // fixed AR=1.7 to match the texture
				float const pos(rgen.rand_uniform((c.d[!dim][0] + 0.6*mwidth), (c.d[!dim][1] - 0.6*mwidth)));
				set_cube_zvals(mwave, c.z2(), c.z2()+mheight);
				set_wall_width(mwave, pos, 0.5*mwidth, !dim);
				mwave.d[dim][ dir] = wall_pos + dir_sign*0.05*mdepth;
				mwave.d[dim][!dir] = mwave.d[dim][dir] + dir_sign*mdepth;
				objs.emplace_back(mwave, TYPE_MWAVE, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt);
				objs[cabinet_id].flags |= RO_FLAG_ADJ_TOP; // flag as having a microwave so that we don't add a book or bottle that could overlap it
				placed_mwave = 1;
			}
			// place a toaster on a counter 90% of the time
			if (!is_sink && !placed_toaster && have_toaster && rgen.rand_float() < 0.9) {
				float const theight(0.09*vspace), twidth(theight*toaster_sz.x/toaster_sz.z), tdepth(theight*toaster_sz.y/toaster_sz.z);

				if (c.get_sz_dim(!dim) > 1.25*twidth && c.get_sz_dim(dim) > 1.25*tdepth) { // add if it fits
					float const pos_w(rgen.rand_uniform((c.d[!dim][0] + 0.6*twidth), (c.d[!dim][1] - 0.6*twidth)));
					float const pos_d(rgen.rand_uniform((c.d[ dim][0] + 0.6*tdepth), (c.d[ dim][1] - 0.6*tdepth)));
					set_cube_zvals(toaster, c.z2(), c.z2()+theight);
					set_wall_width(toaster, pos_w, 0.5*twidth, !dim);
					set_wall_width(toaster, pos_d, 0.5*tdepth,  dim);

					if (!placed_mwave || !mwave.intersects(toaster)) { // don't overlap the microwave
						unsigned const NUM_TOASTER_COLORS = 7;
						colorRGBA const toaster_colors[NUM_TOASTER_COLORS] = {WHITE, LT_GRAY, GRAY, DK_GRAY, GRAY_BLACK, colorRGBA(0.0, 0.0, 0.5), colorRGBA(0.5, 0.0, 0.0)};
						objs.emplace_back(toaster, TYPE_TOASTER, room_id, !dim, rgen.rand_bool(), RO_FLAG_NOCOLL, tot_light_amt); // random dir
						objs.back().color = toaster_colors[rgen.rand()%NUM_TOASTER_COLORS];
						objs[cabinet_id].flags |= RO_FLAG_ADJ_TOP; // flag as having a toaster so that we don't add a book or bottle that could overlap it
						placed_toaster = 1;
					}
				}
			}
			if (is_sink) { // kitchen sink; add cups, plates, and cockroaches
				cube_t sink(get_sink_cube(objs[cabinet_id]));
				sink.z2() = sink.z1(); // shrink to zero area at the bottom
				unsigned const objs_start(objs.size()), num_objs(1 + rgen.rand_bool()); // 1-2 objects

				for (unsigned n = 0; n < num_objs; ++n) {
					unsigned const obj_type(rgen.rand()%3);
					cube_t avoid;
					if (objs.size() > objs_start) {avoid = objs.back();} // avoid the last object that was placed, if there was one

					if      (obj_type == 0) {place_plate_on_obj(rgen, sink, room_id, tot_light_amt);} // add a plate
					else if (obj_type == 1) {place_cup_on_obj  (rgen, sink, room_id, tot_light_amt);} // add a cup
					else if (obj_type == 2 && building_obj_model_loader.is_model_valid(OBJ_MODEL_ROACH)) { // add a cockroach (upside down?)
						sink.d[dim][!dir] = sink.get_center_dim(dim); // use the half area near the back wall to make sure the roach is visible to the player
						cube_t roach;
						float const radius(sink.get_sz_dim(dim)*rgen.rand_uniform(0.08, 0.12)), height(get_cockroach_height_from_radius(radius));
						gen_xy_pos_for_round_obj(roach, sink, radius, height, 1.1*radius, rgen);
						objs.emplace_back(roach, TYPE_ROACH, room_id, 0, 0, (RO_FLAG_NOCOLL | RO_FLAG_RAND_ROT), tot_light_amt);
						if (rgen.rand_bool()) {objs.back().flags |= RO_FLAG_BROKEN;} // 50% chance it's dead
					}
				} // for n
			}
			is_sink = 0; // sink is in first placed counter only
		} // for n
	}
	return placed_obj;
}

bool building_t::add_livingroom_objs(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start) {
	if (!is_house || room.is_hallway || room.is_sec_bldg || room.is_office) return 0; // these can't be living rooms
	float const wall_thickness(get_wall_thickness());
	cube_t place_area(get_walkable_room_bounds(room));
	place_area.expand_by(-0.25*wall_thickness); // common spacing to wall for appliances
	vect_room_object_t &objs(interior->room_geom->objs);
	bool placed_couch(0), placed_tv(0);
	// place couches with a variety of colors
	unsigned const NUM_COLORS = 8;
	colorRGBA const colors[NUM_COLORS] = {GRAY_BLACK, WHITE, LT_GRAY, GRAY, DK_GRAY, LT_BROWN, BROWN, DK_BROWN};
	colorRGBA const &couch_color(colors[rgen.rand()%NUM_COLORS]);
	unsigned tv_pref_orient(4), couch_ix(objs.size()), tv_ix(0);
	
	if (place_model_along_wall(OBJ_MODEL_COUCH, TYPE_COUCH, room, 0.40, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 0.67, 4, 1, couch_color)) { // pref centered
		placed_couch   = 1;
		tv_pref_orient = (2*objs[couch_ix].dim + !objs[couch_ix].dir); // TV should be across from couch
	}
	tv_ix = objs.size();

	// place TV: pref centered; maybe should set not_at_window=1, but that seems too restrictive
	if (place_model_along_wall(OBJ_MODEL_TV, TYPE_TV, room, 0.45, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 4.0, tv_pref_orient, 1, BKGRAY, 0)) {
		placed_tv = 1;
		// add a small table to place the TV on so that it's off the floor and not blocked as much by tables and chairs
		room_object_t &tv(objs[tv_ix]);
		float const height(0.4*tv.dz());
		cube_t table(tv); // same XY bounds as the TV
		tv.translate_dim(2, height); // move TV up
		table.z2() = tv.z1();
		objs.emplace_back(table, TYPE_TABLE, room_id, 0, 0, RO_FLAG_IS_HOUSE, tot_light_amt, SHAPE_SHORT); // short table; houses only
	}
	if (placed_couch && placed_tv) {
		room_object_t const &couch(objs[couch_ix]), &tv(objs[tv_ix]);

		if (couch.dim == tv.dim && couch.dir != tv.dir) { // placed against opposite walls facing each other
			cube_t region(couch);
			region.union_with_cube(tv);
			shorten_chairs_in_region(region, objs_start); // region represents that space between the couch and the TV
		}
	}
	if (!placed_couch && !placed_tv) return 0; // not a living room

	if (rgen.rand_bool()) {
		unsigned const chair_ix(objs.size());
		cube_t chair_place_area(place_area);
		chair_place_area.expand_by(-wall_thickness); // move a bit further back from the wall to prevent intersections when rotating

		if (place_model_along_wall(OBJ_MODEL_RCHAIR, TYPE_RCHAIR, room, 0.45, rgen, zval, room_id, tot_light_amt, chair_place_area, objs_start, 1.0)) {
			if (rgen.rand_bool()) { // add a random rotation half the time
				assert(chair_ix < objs.size()); // chair must have been placed
				objs[chair_ix].flags |= RO_FLAG_RAND_ROT; // rotate to face the center of the room rather than having it be random?
				objs[chair_ix].shape  = SHAPE_CYLIN; // make it a cylinder since it no longer fits in a tight cube
			}
		}
	}
	return 1;
}

// Note: this room is decided by the caller and the failure to add objects doesn't make it not a dining room
void building_t::add_diningroom_objs(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start) {
	//if (!is_house || room.is_hallway || room.is_sec_bldg || room.is_office) return; // still applies, but unnecessary
	if ((rgen.rand()&3) == 0) return; // no additional objects 25% of the time
	cube_t room_bounds(get_walkable_room_bounds(room));
	room_bounds.expand_by_xy(-get_trim_thickness());
	float const vspace(get_window_vspace()), clearance(max(0.2f*vspace, get_min_front_clearance_inc_people()));
	vect_room_object_t &objs(interior->room_geom->objs);
	// add a wine rack
	float const width(0.3*vspace*rgen.rand_uniform(1.0, 1.5)), depth(0.16*vspace), height(0.4*vspace*rgen.rand_uniform(1.0, 1.5)); // depth is based on bottle length, which is constant
	cube_t c;
	set_cube_zvals(c, zval, zval+height);

	for (unsigned n = 0; n < 10; ++n) { // make 10 attempts to place a wine rack; similar to placing a bookcase
		bool const dim(rgen.rand_bool()), dir(rgen.rand_bool()); // choose a random wall
		c.d[dim][ dir] = room_bounds.d[dim][dir]; // against this wall
		c.d[dim][!dir] = c.d[dim][dir] + (dir ? -1.0 : 1.0)*depth;
		float const pos(rgen.rand_uniform(room_bounds.d[!dim][0]+0.5*width, room_bounds.d[!dim][1]-0.5*width));
		set_wall_width(c, pos, 0.5*width, !dim);
		cube_t tc(c);
		tc.d[dim][!dir] += (dir ? -1.0 : 1.0)*clearance; // increase space to add clearance
		if (is_obj_placement_blocked(tc, room, 1) || overlaps_other_room_obj(tc, objs_start)) continue; // bad placement
		objs.emplace_back(c, TYPE_WINE_RACK, room_id, dim, !dir, 0, tot_light_amt); // Note: dir faces into the room, not the wall
		set_obj_id(objs);
		break; // done/success
	} // for n
}

bool building_t::add_library_objs(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, bool is_basement) {
	if (room.is_hallway || room.is_sec_bldg) return 0; // these can't be libraries
	unsigned num_added(0);

	for (unsigned n = 0; n < 8; ++n) { // place up to 8 bookcases
		bool const added(add_bookcase_to_room(rgen, room, zval, room_id, tot_light_amt, objs_start, is_basement));
		if (added) {++num_added;} else {break;}
	}
	if (num_added == 0) return 0;
	if (!is_house) {add_door_sign("Library", room, zval, room_id, tot_light_amt);} // add office building library sign
	return 1;
}

void gen_crate_sz(vector3d &sz, rand_gen_t &rgen, float window_vspacing) {
	for (unsigned d = 0; d < 3; ++d) {sz[d] = 0.06*window_vspacing*(1.0 + ((d == 2) ? 1.2 : 2.0)*rgen.rand_float());} // slightly more variation in XY
}

bool building_t::add_storage_objs(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, bool is_basement) {
	bool const is_garage_or_shed(room.is_garage_or_shed(0)), is_int_garage(room.get_room_type(0) == RTYPE_GARAGE);
	float const window_vspacing(get_window_vspace()), wall_thickness(get_wall_thickness()), floor_thickness(get_floor_thickness());
	float const ceil_zval(zval + window_vspacing - floor_thickness), shelf_depth((is_house ? (is_basement ? 0.18 : 0.15) : 0.2)*window_vspacing);
	float shelf_shorten(shelf_depth + 1.0f*wall_thickness);
	// increase shelf shorten for interior garages to account for approx width of exterior door when opened
	if (is_int_garage) {max_eq(shelf_shorten, 0.36f*window_vspacing);}
	cube_t room_bounds(get_walkable_room_bounds(room)), crate_bounds(room_bounds);
	vect_room_object_t &objs(interior->room_geom->objs);
	unsigned const num_crates(4 + (rgen.rand() % (is_house ? (is_basement ? 12 : 5) : 30))); // 4-33 for offices, 4-8 for houses, 4-16 for house basements
	vect_cube_t exclude;
	cube_t test_cube(room);
	set_cube_zvals(test_cube, zval, zval+wall_thickness); // reduce to a small z strip for this floor to avoid picking up doors on floors above or below
	unsigned num_placed(0), num_doors(0);

	// first pass to count the number of doors in this room
	for (auto i = interior->door_stacks.begin(); i != interior->door_stacks.end(); ++i) {
		num_doors += is_cube_close_to_door(test_cube, 0.0, 0, *i, 2); // check both dirs
	}
	for (auto i = interior->door_stacks.begin(); i != interior->door_stacks.end(); ++i) {
		if (!is_cube_close_to_door(test_cube, 0.0, 0, *i, 2)) continue; // wrong room; check both dirs
		exclude.push_back(*i);
		exclude.back().expand_in_dim( i->dim, 0.6*room.get_sz_dim(i->dim));
		// if there are multiple doors (houses only?), expand the exclude area more in the other dimension to make sure there's a path between doors
		float const path_expand(((num_doors > 1) ? min(1.2f*i->get_width(), 0.3f*room.get_sz_dim(!i->dim)) : 0.0));
		exclude.back().expand_in_dim(!i->dim, path_expand);
		exclude.back().union_with_cube(i->get_open_door_bcube_for_room(room)); // include open door
	}
	// add shelves on walls (avoiding any door(s)), and have crates avoid them
	for (unsigned dim = 0; dim < 2; ++dim) {
		if (room_bounds.get_sz_dim( dim) < 6.0*shelf_depth  ) continue; // too narrow to add shelves in this dim
		if (room_bounds.get_sz_dim(!dim) < 4.0*shelf_shorten) continue; // too narrow in the other dim

		for (unsigned dir = 0; dir < 2; ++dir) {
			if (is_int_garage ? ((rgen.rand()%3) == 0) : rgen.rand_bool()) continue; // only add shelves to 50% of the walls, 67% for interior garages
			
			if (is_garage_or_shed) {
				// garage or shed - don't place shelves in front of door, but allow them against windows; basement - don't place against basement door
				cube_t wall(room);
				wall.d[dim][!dir] = wall.d[dim][dir]; // shrink room to zero width along this wall
				if (is_room_adjacent_to_ext_door(wall)) continue;
			}
			else if (is_house && !is_basement && has_windows() && classify_room_wall(room, zval, dim, dir, 0) == ROOM_WALL_EXT) {
				// don't place shelves against exterior house walls in case there are windows
				cube_t const part(get_part_for_room(room));
				float const h_spacing(get_hspacing_for_part(part, !dim));
				if (room_bounds.get_sz_dim(!dim) - 2.0*shelf_depth > h_spacing) continue; // shelf width is larger than spacing - likely to intersect a window, don't test center pt
				if (is_val_inside_window(part, !dim, room_bounds.get_center_dim(!dim), h_spacing, get_window_h_border())) continue;
			}
			cube_t shelves(room_bounds);
			set_cube_zvals(shelves, zval, ceil_zval-floor_thickness);
			crate_bounds.d[dim][dir] = shelves.d[dim][!dir] = shelves.d[dim][dir] + (dir ? -1.0 : 1.0)*shelf_depth; // outer edge of shelves, which is also the crate bounds
			shelves.expand_in_dim(!dim, -shelf_shorten); // shorten shelves
			cube_t cands[3] = {shelves, shelves, shelves}; // full, lo half, hi half
			cands[1].d[!dim][1] = cands[2].d[!dim][0] = shelves.get_center_dim(!dim); // split in half
			unsigned const num_cands((cands[1].get_sz_dim(!dim) < 4.0*shelf_shorten) ? 1 : 3); // only check full length if short

			for (unsigned n = 0; n < num_cands; ++n) {
				cube_t const &cand(cands[n]);
				if (has_bcube_int(cand, exclude)) continue; // too close to a doorway
				if (!is_garage_or_shed && interior->is_blocked_by_stairs_or_elevator(cand)) continue;
				if (overlaps_other_room_obj(cand, objs_start)) continue; // can be blocked by bookcase, etc.
				unsigned const shelf_flags((is_house ? RO_FLAG_IS_HOUSE : 0) | (is_garage_or_shed ? 0 : RO_FLAG_INTERIOR));
				objs.emplace_back(cand, TYPE_SHELVES, room_id, dim, dir, shelf_flags, tot_light_amt);
				set_obj_id(objs);
				break; // done
			} // for n
		} // for dir
	} // for dim
	if (is_garage_or_shed) return 1; // no chair, crates, or boxes in garages or sheds

	// add a random office chair if there's space
	if (!is_house && min(crate_bounds.dx(), crate_bounds.dy()) > 1.2*window_vspacing && building_obj_model_loader.is_model_valid(OBJ_MODEL_OFFICE_CHAIR)) {
		float const chair_height(0.425*window_vspacing), chair_radius(0.5f*chair_height*get_radius_for_square_model(OBJ_MODEL_OFFICE_CHAIR));
		point const pos(gen_xy_pos_in_area(crate_bounds, chair_radius, rgen, zval));
		cube_t chair(get_cube_height_radius(pos, chair_radius, chair_height));
		
		// for now, just make one random attempt; if it fails then there's no chair in this room
		if (!has_bcube_int(chair, exclude) && !is_obj_placement_blocked(chair, room, 1)) {
			objs.emplace_back(chair, TYPE_OFF_CHAIR, room_id, rgen.rand_bool(), rgen.rand_bool(), RO_FLAG_RAND_ROT, tot_light_amt, SHAPE_CYLIN, GRAY_BLACK);
		}
	}
	door_path_checker_t door_path_checker;

	for (unsigned n = 0; n < 4*num_crates; ++n) { // make up to 4 attempts for every crate/box
		vector3d sz; // half size relative to window_vspacing
		gen_crate_sz(sz, rgen, window_vspacing*(is_house ? (is_basement ? 0.75 : 0.5) : 1.0)); // smaller for houses
		if (crate_bounds.dx() <= 2.0*sz.x || crate_bounds.dy() <= 2.0*sz.y) continue; // too large for this room
		point const pos(gen_xy_pos_in_area(crate_bounds, sz, rgen, zval));
		cube_t crate(get_cube_height_radius(pos, sz, 2.0*sz.z)); // multiply by 2 since this is a size rather than half size/radius
		if (has_bcube_int(crate, exclude)) continue; // don't place crates between the door and the center of the room
		bool bad_placement(0);

		for (auto i = objs.begin()+objs_start; i != objs.end(); ++i) {
			if (!i->intersects(crate)) continue;
			// only handle stacking of crates on other crates
			if ((i->type == TYPE_CRATE || i->type == TYPE_BOX) && i->z1() == zval && (i->z2() + crate.dz() < ceil_zval) && i->contains_pt_xy(pos)) {crate.translate_dim(2, i->dz());}
			else {bad_placement = 1; break;}
		}
		if (bad_placement) continue;
		if (is_obj_placement_blocked(crate, room, 1)) continue;
		if (door_path_checker.check_door_path_blocked(crate, room, zval, *this)) continue; // don't block the path between doors
		cube_t c2(crate);
		c2.expand_by(vector3d(0.5*c2.dx(), 0.5*c2.dy(), 0.0)); // approx extents of flaps if open
		unsigned flags(0);
		
		for (unsigned d = 0; d < 4; ++d) { // determine which sides are against a wall
			bool const dim(d>>1), dir(d&1);
			if ((c2.d[dim][dir] < room_bounds.d[dim][dir]) ^ dir) {flags |= (RO_FLAG_ADJ_LO << d);}
		}
		objs.emplace_back(crate, (rgen.rand_bool() ? TYPE_CRATE : TYPE_BOX), room_id, rgen.rand_bool(), 0, flags, tot_light_amt, SHAPE_CUBE, gen_box_color(rgen)); // crate or box
		set_obj_id(objs); // used to select texture and box contents
		if (++num_placed == num_crates) break; // we're done
	} // for n
	// add office building storage room sign, in a hallway, basement, etc.
	if (!is_house /*&& !is_basement*/) {add_door_sign("Storage", room, zval, room_id, tot_light_amt);}
	return 1; // it's always a storage room, even if it's empty
}

void building_t::add_garage_objs(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt) {
	if (!enable_parked_cars() || (rgen.rand()&3) == 0) return; // 75% of garages have cars
	unsigned const flags(RO_FLAG_NOCOLL | RO_FLAG_USED | RO_FLAG_INVIS); // lines not shown
	bool const dim(room.dx() < room.dy()); // long dim
	bool dir(0); // set dir so that cars pull into driveways
	if (street_dir > 0 && bool((street_dir-1)>>1) == dim) {dir = !((street_dir-1)&1);} // use street_dir if it's set and dims agree
	else {dir = (room.get_center_dim(dim) < bcube.get_center_dim(dim));} // assumes the garage is at an exterior wall and doesn't occupy the entire house width
	vect_room_object_t &objs(interior->room_geom->objs);
	cube_t space(room); // full room, car will be centered here
	set_cube_zvals(space, zval, (zval + 0.001*get_window_vspace()));
	room_object_t pspace(space, TYPE_PARK_SPACE, room_id, dim, dir, flags, tot_light_amt, SHAPE_CUBE, WHITE);
	pspace.obj_id = (uint16_t)(objs.size() + rgen.rand()); // will be used for the car model and color
	car_t const car(car_from_parking_space(pspace));
	interior->room_geom->wall_ps_start = objs.size(); // first parking space index
	cube_t collider(car.bcube);
	float const min_spacing(2.1*get_scaled_player_radius()); // space for the player to fit

	for (unsigned d = 0; d < 2; ++d) { // make sure there's enough spacing around the car for the player to walk without getting stuck
		max_eq(collider.d[d][0], (room.d[d][0] + min_spacing));
		min_eq(collider.d[d][1], (room.d[d][1] - min_spacing));
	}
	if (!collider.is_strictly_normalized()) {collider = car.bcube;} // garage is too small for player to fit; shouldn't happen
	objs.push_back(pspace);
	objs.emplace_back(collider, TYPE_COLLIDER, room_id, dim, dir, (RO_FLAG_INVIS | RO_FLAG_FOR_CAR));
	interior->room_geom->has_garage_car = 1;
}

void building_t::add_floor_clutter_objs(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start) {
	if (!is_house) return; // houses only for now

	if (rgen.rand_float() < 0.10) { // maybe add a toy 10% of the time
		vect_room_object_t &objs(interior->room_geom->objs);

		for (auto i = objs.begin()+objs_start; i != objs.end(); ++i) {
			if (i->type == TYPE_TOY) return; // don't place a toy on both a room object and on the floor
		}
		bool const use_model(building_obj_model_loader.is_model_valid(OBJ_MODEL_TOY));
		float const window_vspacing(get_window_vspace()), wall_thickness(get_wall_thickness());
		cube_t place_area(get_walkable_room_bounds(room));
		place_area.expand_by(-1.0*wall_thickness); // add some extra padding
		float const height(0.11*window_vspacing), radius(0.5f*height*(use_model ? get_radius_for_square_model(OBJ_MODEL_TOY) : 0.67f));

		if (radius < 0.1*min(place_area.dx(), place_area.dy())) {
			point const pos(gen_xy_pos_in_area(place_area, radius, rgen, zval));
			cube_t c(get_cube_height_radius(pos, radius, height));

			// for now, just make one random attempt; if it fails then there's no chair in this room
			if (!overlaps_other_room_obj(c, objs_start) && !is_obj_placement_blocked(c, room, 1)) {
				if (use_model) { // symmetric, no dim or dir, but random rotation
					objs.emplace_back(c, TYPE_TOY_MODEL, room_id, 0, 0, (RO_FLAG_RAND_ROT | RO_FLAG_NOCOLL), tot_light_amt);
				}
				else { // random dim/dir
					objs.emplace_back(c, TYPE_TOY, room_id, rgen.rand_bool(), rgen.rand_bool(), RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN);
					set_obj_id(objs); // used for color selection
				}
			}
		}
	}
}

void building_t::add_laundry_basket(rand_gen_t &rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, cube_t place_area) {
	float const floor_spacing(get_window_vspace()), radius(rgen.rand_uniform(0.1, 0.12)*floor_spacing), height(rgen.rand_uniform(1.5, 2.2)*radius);
	place_area.expand_by_xy(-radius); // leave a slight gap between laundry basket and wall
	if (!place_area.is_strictly_normalized()) return; // no space for laundry basket (likely can't happen)
	cube_t legal_area(get_part_for_room(room));
	legal_area.expand_by_xy(-(1.0*floor_spacing + radius)); // keep away from part edge/exterior walls to avoid alpha mask drawing problems (unless we use mats_amask)
	point center;
	center.z = zval + 0.002*floor_spacing; // slightly above the floor to avoid z-fighting

	for (unsigned n = 0; n < 20; ++n) { // make 20 attempts to place a laundry basket
		bool const dim(rgen.rand_bool()), dir(rgen.rand_bool()); // choose a random wall
		center[ dim] = place_area.d[dim][dir]; // against this wall
		center[!dim] = rgen.rand_uniform(place_area.d[!dim][0], place_area.d[!dim][1]);
		if (!legal_area.contains_pt_xy(center)) continue; // too close to part edge
		cube_t const c(get_cube_height_radius(center, radius, height));
		if (is_obj_placement_blocked(c, room, !room.is_hallway) || overlaps_other_room_obj(c, objs_start)) continue; // bad placement
		colorRGBA const colors[4] = {WHITE, LT_BLUE, LT_GREEN, LT_BROWN};
		interior->room_geom->objs.emplace_back(c, TYPE_LBASKET, room_id, dim, dir, 0, tot_light_amt, SHAPE_CYLIN, colors[rgen.rand()%4]);
		break; // done
	} // for n
}

bool building_t::add_laundry_objs(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, unsigned &added_bathroom_objs_mask) {
	float const front_clearance(get_min_front_clearance_inc_people());
	cube_t place_area(get_walkable_room_bounds(room));
	place_area.expand_by(-0.25*get_wall_thickness()); // common spacing to wall for appliances
	vector3d const place_area_sz(place_area.get_size());
	vect_room_object_t &objs(interior->room_geom->objs);

	for (unsigned n = 0; n < 10; ++n) { // 10 attempts to place washer and dryer along the same wall
		unsigned const washer_ix(objs.size());
		bool const placed_washer(place_model_along_wall(OBJ_MODEL_WASHER, TYPE_WASHER, room, 0.42, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 0.8));
		unsigned pref_orient(4); // if washer was placed, prefer to place dryer along the same wall
		if (placed_washer) {pref_orient = objs[washer_ix].get_orient();}
		unsigned const dryer_ix(objs.size());
		bool const placed_dryer(place_model_along_wall(OBJ_MODEL_DRYER, TYPE_DRYER, room, 0.38, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 0.8, pref_orient));
		bool success(0);
		if (placed_washer && placed_dryer && objs[dryer_ix].get_orient() == pref_orient) {success = 1;} // placed both washer and dryer along the same wall
		else if (n+1 == 10) { // last attempt
			if (!(placed_washer || placed_dryer)) return 0; // placed neither washer nor dryer, failed
			if (placed_washer != placed_dryer) {success = 1;} // placed only one of the washer or dryer, allow it
			else if (objs[washer_ix].dim != objs[dryer_ix].dim) {success = 1;} // placed on two adjacent walls, allow it
			// placed on opposite walls; check that there's space for the player to walk between the washer and dryer
			else if (objs[washer_ix].get_sz_dim(objs[washer_ix].dim) + objs[dryer_ix].get_sz_dim(objs[dryer_ix].dim) + front_clearance < place_area_sz[objs[washer_ix].dim]) {success = 1;}
		}
		if (success) {
			// if we've placed a washer and/or dryer and made this into a laundry room, try to place a sink as well; should this use a different sink model from bathrooms?
			if (place_model_along_wall(OBJ_MODEL_SINK, TYPE_SINK, room, 0.45, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 0.6)) {
				added_bathroom_objs_mask |= PLACED_SINK;
			}
			add_laundry_basket(rgen, room, zval, room_id, tot_light_amt, objs_start, place_area); // try to place a laundry basket
			return 1; // done
		}
		objs.resize(objs_start); // remove washer and dryer and try again
	} // for n
	return 0; // failed
}

bool get_fire_ext_height_and_radius(float window_vspacing, float &height, float &radius) {
	if (!building_obj_model_loader.is_model_valid(OBJ_MODEL_FIRE_EXT)) return 0;
	vector3d const sz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_FIRE_EXT)); // D, W, H
	height = 0.16*window_vspacing;
	radius = height*(0.5*(sz.x + sz.y)/sz.z);
	return 1;
}
void building_t::add_fire_ext(float height, float radius, float zval, float wall_edge, float pos_along_wall, unsigned room_id, float tot_light_amt, bool dim, bool dir) {
	float const window_vspacing(get_window_vspace()), dir_sign(dir ? -1.0 : 1.0);
	point pos(0.0, 0.0, (zval + 0.32*window_vspacing)); // bottom position
	pos[ dim] = wall_edge + dir_sign*radius; // radius away from the wall
	pos[!dim] = pos_along_wall;

	vect_room_object_t &objs(interior->room_geom->objs);
	// add fire extinguisher
	cube_t fe_bcube(pos, pos);
	fe_bcube.expand_by_xy(radius);
	fe_bcube.z2() += height;
	objs.emplace_back(fe_bcube, TYPE_FIRE_EXT, room_id, !dim, (dir ^ dim), RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN); // mounted sideways
	// add the wall mounting bracket; what about adding a small box with a door that contains the fire extinguisher?
	cube_t wall_mount(fe_bcube);
	wall_mount.expand_in_dim(!dim, -0.52*radius);
	wall_mount.translate_dim(!dim, ((dim ^ dir ^ 1) ? 1.0 : -1.0)*0.24*radius); // shift to line up with FE body
	wall_mount.d[dim][ dir]  = wall_edge; // extend to touch the wall
	wall_mount.d[dim][!dir] -= dir_sign*0.8*radius; // move inward
	wall_mount.z1() -= 0.02*height; // under the fire extinguisher
	wall_mount.z2() -= 0.30*height;
	objs.emplace_back(wall_mount, TYPE_FEXT_MOUNT, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CUBE, GRAY_BLACK);
	// add the sign
	cube_t sign;
	sign.d[dim][ dir] = wall_edge; // extend to touch the wall
	sign.d[dim][!dir] = wall_edge + dir_sign*0.05*radius;
	set_cube_zvals(sign, (zval + 0.65*window_vspacing), (zval + 0.80*window_vspacing));
	set_wall_width(sign, wall_mount.get_center_dim(!dim), 0.5*radius, !dim); // line up with wall bracket
	objs.emplace_back(sign, TYPE_FEXT_SIGN, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt);
}

void building_t::add_pri_hall_objs(rand_gen_t rgen, rand_gen_t room_rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned floor_ix) {
	bool const long_dim(room.dx() < room.dy());
	float const window_vspacing(get_window_vspace());
	vect_room_object_t &objs(interior->room_geom->objs);

	if (floor_ix == 0) { // place first floor objects
		// reception desks
		float const desk_width(0.9*window_vspacing);
		
		if (room.get_sz_dim(!long_dim) > (desk_width + 1.6*get_doorway_width())) { // hallway is wide enough for a reception desk
			float const centerline(room.get_center_dim(!long_dim)), desk_depth(0.6*desk_width);
			cube_t desk;
			set_cube_zvals(desk, zval, zval+0.32*window_vspacing);
			set_wall_width(desk, centerline, 0.5*desk_width, !long_dim);

			for (unsigned dir = 0; dir < 2; ++dir) { // add a reception desk at each entrance
				float const hall_len(room.get_sz_dim(long_dim)), hall_start(room.d[long_dim][dir]), dir_sign(dir ? -1.0 : 1.0);
				float const val1(hall_start + max(0.1f*hall_len, window_vspacing)*dir_sign), val2(hall_start + 0.3*hall_len*dir_sign); // range of reasonable desk placements along the hall

				for (unsigned n = 0; n < 10; ++n) { // try to find the closest valid placement to the door, make 10 random attempts
					float const val(rgen.rand_uniform(min(val1, val2), max(val1, val2)));
					set_wall_width(desk, val, 0.5*desk_depth, long_dim);
					if (interior->is_blocked_by_stairs_or_elevator(desk)) continue; // bad location, try a new one

					if (building_obj_model_loader.is_model_valid(OBJ_MODEL_OFFICE_CHAIR)) {
						float const chair_height(0.425*window_vspacing), chair_radius(0.5f*chair_height*get_radius_for_square_model(OBJ_MODEL_OFFICE_CHAIR));
						point pos;
						pos.z = zval;
						pos[!long_dim] = centerline;
						pos[ long_dim] = val + dir_sign*(-0.05*desk_depth + chair_radius); // push the chair into the cutout of the desk
						cube_t const chair(get_cube_height_radius(pos, chair_radius, chair_height));
						if (interior->is_blocked_by_stairs_or_elevator(chair)) continue; // bad location, try a new one
						objs.emplace_back(chair, TYPE_OFF_CHAIR, room_id, long_dim, dir, 0, tot_light_amt, SHAPE_CYLIN, GRAY_BLACK);
					}
					objs.emplace_back(desk, TYPE_RDESK, room_id, long_dim, dir, 0, tot_light_amt, SHAPE_CUBE);
					break; // done
				} // for n
			} // for dir
		}
	}
	float fe_height(0.0), fe_radius(0.0);

	if (get_fire_ext_height_and_radius(window_vspacing, fe_height, fe_radius)) { // add a fire extinguisher on the wall
		float const min_clearance(2.0*fe_radius), wall_pos_lo(room.d[long_dim][0] + min_clearance), wall_pos_hi(room.d[long_dim][1] - min_clearance);

		if (wall_pos_lo < wall_pos_hi) { // should always be true?
			bool const dir(room_rgen.rand_bool()); // random, but the same across all floors
			float const wall_pos(room.d[!long_dim][dir] + (dir ? -1.0 : 1.0)*0.5*get_wall_thickness());

			for (unsigned n = 0; n < 20; ++n) { // make 20 attempts at placing a fire extinguisher
				float const val(room_rgen.rand_uniform(wall_pos_lo, wall_pos_hi)), cov_lo(val - min_clearance), cov_hi(val + min_clearance);
				bool contained_in_wall(0);

				for (cube_t const &wall : interior->walls[!long_dim]) {
					if (wall.d[!long_dim][0] > wall_pos || wall.d[!long_dim][1] < wall_pos) continue; // not on the correct side of this hallway
					if (wall.d[ long_dim][0] > cov_lo   || wall.d[ long_dim][1] < cov_hi  ) continue; // range not covered
					if (wall.z1() > zval || wall.z2() < zval) continue; // wrong zval/floor
					contained_in_wall = 1; break;
				}
				if (contained_in_wall) { // shouldn't need to check anything else?
					add_fire_ext(fe_height, fe_radius, zval, wall_pos, val, room_id, tot_light_amt, !long_dim, dir);
					break; // done/success
				}
			} // for n
		}
	}
}

bool building_t::add_server_room_objs(rand_gen_t rgen, room_t const &room, float &zval, unsigned room_id, float tot_light_amt, unsigned objs_start) { // for office buildings
	float const window_vspacing(get_window_vspace());
	float const server_height(0.7*window_vspacing*rgen.rand_uniform(0.9, 1.1));
	float const server_width (0.3*window_vspacing*rgen.rand_uniform(0.9, 1.1)), server_hwidth(0.5*server_width);
	float const server_depth (0.4*window_vspacing*rgen.rand_uniform(0.9, 1.1)), server_hdepth(0.5*server_depth);
	float const comp_height  (0.2*window_vspacing*rgen.rand_uniform(0.9, 1.1));
	float const min_spacing  (0.1*window_vspacing*rgen.rand_uniform(0.9, 1.1));
	float const comp_hwidth(0.5*0.44*comp_height), comp_hdepth(0.5*0.9*comp_height); // fixed AR=0.44 to match the texture
	float const server_period(server_width + min_spacing);
	bool const long_dim(room.dx() < room.dy());
	cube_t place_area(get_walkable_room_bounds(room));
	place_area.expand_by(-0.25*get_wall_thickness()); // server spacing from walls
	zval = add_flooring(room, zval, room_id, tot_light_amt, FLOORING_CONCRETE); // add concreate and move the effective floor up
	cube_t server, computer;
	set_cube_zvals(server,   zval, (zval + server_height));
	set_cube_zvals(computer, zval, (zval + comp_height  ));
	point center;
	unsigned num_servers(0), num_comps(0);
	vect_room_object_t &objs(interior->room_geom->objs);

	// try to line servers up against each wall wherever they fit
	for (unsigned D = 0; D < 2; ++D) {
		bool const dim(bool(D) ^ long_dim); // place along walls in long dim first
		float const room_len(place_area.get_sz_dim(dim));
		unsigned const num(room_len/server_period); // take the floor
		if (num == 0) continue; // not enough space for a server in this dim
		float const server_spacing(room_len/num);
		center[dim] = place_area.d[dim][0] + 0.5*server_spacing; // first position at half spacing

		for (unsigned n = 0; n < num; ++n, center[dim] += server_spacing) {
			set_wall_width(server, center[dim], server_hwidth, dim); // position along the wall

			for (unsigned dir = 0; dir < 2; ++dir) {
				float const dir_sign(dir ? -1.0 : 1.0);
				center[!dim] = place_area.d[!dim][dir] + dir_sign*server_hdepth;
				set_wall_width(server, center[!dim], server_hdepth, !dim); // position from the wall
				
				// Note: overlaps_other_room_obj includes previously placed servers, so we don't have to check for intersections at the corners of rooms
				if (is_obj_placement_blocked(server, room, 1) || overlaps_other_room_obj(server, objs_start)) { // no space for server; try computer instead
					set_wall_width(computer,  center[ dim], comp_hwidth, dim); // position along the wall
					set_wall_width(computer, (place_area.d[!dim][dir] + 1.2*dir_sign*comp_hdepth), comp_hdepth, !dim); // position from the wall
					if (is_obj_placement_blocked(computer, room, 1) || overlaps_other_room_obj(computer, objs_start)) continue;
					objs.emplace_back(computer, TYPE_COMPUTER, room_id, !dim, !dir, 0, tot_light_amt);
					++num_comps;
					continue;
				}
				objs.emplace_back(server, TYPE_SERVER, room_id, !dim, !dir, 0, tot_light_amt);
				cube_t blocker(server);
				blocker.d[!dim][ dir]  = server.d[!dim][!dir]; // front of server
				blocker.d[!dim][!dir] += dir_sign*server_width; // add space in the front for the door to open (don't block with another server)
				objs.emplace_back(blocker, TYPE_BLOCKER, room_id, dim, 0, RO_FLAG_INVIS);
				++num_servers;
			} // for dir
		} // for n
	} // for dim
	if (num_servers == 0 && num_comps == 0) return 0; // both servers and computers count
	
	if (num_servers > 0) { // add a keyboard to the master server
		unsigned const master_server(rgen.rand() % num_servers);

		for (unsigned i = objs_start, server_ix = 0; i < objs.size(); ++i) {
			room_object_t const &server(objs[i]);
			if (server.type != TYPE_SERVER  ) continue;
			if (server_ix++ != master_server) continue;
			float const kbd_hwidth(0.8*server_hwidth), kbd_depth(0.6*kbd_hwidth), kbd_height(0.04*kbd_hwidth); // slightly flatter than regular keyboards
			bool const dim(server.dim), dir(server.dir);
			float const kbd_z1(server.z1() + 0.57*server.dz()), server_front(server.d[dim][dir]);
			cube_t keyboard;
			set_cube_zvals(keyboard, kbd_z1, kbd_z1+kbd_height);
			keyboard.d[dim][!dir] = server_front; // at front of server
			keyboard.d[dim][ dir] = server_front + (dir ? 1.0 : -1.0)*kbd_depth; // sticks out of the front
			set_wall_width(keyboard, server.get_center_dim(!dim), kbd_hwidth, !dim);
			if (is_obj_placement_blocked(keyboard, room, 1)) break; // Note: not checking overlaps_other_room_obj() because it will overlap server blockers
			objs.emplace_back(keyboard, TYPE_KEYBOARD, room_id, dim, dir, RO_FLAG_HANGING, tot_light_amt); // add as white, will be drawn with gray/black texture
			break;
		} // for i
	}
	// maybe add laptops on top of some servers, to reward the player for finding this room
	for (unsigned i = objs_start; i < objs.size(); ++i) {
		room_object_t const &server(objs[i]);
		if (server.type != TYPE_SERVER) continue;
		if (rgen.rand_float() > 0.2)    continue; // place laptops 20% of the time
		bool const dim(server.dim), dir(server.dir);
		float const server_front(server.d[dim][dir]); // copy before reference is invalidated
		if (!place_laptop_on_obj(rgen, server, room_id, tot_light_amt)) continue; // no avoid, use_dim_dir=0
		// make the laptop hang over the edge of the front of the server so that the player can see and take it
		room_object_t &laptop(objs.back());
		float const xlate(server_front - laptop.d[dim][dir] + (dir ? 1.0 : -1.0)*rgen.rand_uniform(0.05, 0.35)*laptop.get_sz_dim(dim));
		laptop.translate_dim(dim, xlate);
		laptop.flags |= RO_FLAG_HANGING;
	} // for i
	add_door_sign("Server Room", room, zval, room_id, tot_light_amt);
	return 1;
}

void building_t::place_book_on_obj(rand_gen_t &rgen, room_object_t const &place_on, unsigned room_id, float tot_light_amt, unsigned objs_start, bool use_dim_dir) {
	point center(place_on.get_cube_center());
	for (unsigned d = 0; d < 2; ++d) {center[d] += 0.1*place_on.get_sz_dim(d)*rgen.rand_uniform(-1.0, 1.0);} // add a slight random shift
	float const book_sz(0.07*get_window_vspace());
	// book is randomly oriented for tables and rotated 90 degrees from desk orient
	bool const dim(use_dim_dir ? !place_on.dim : rgen.rand_bool()), dir(use_dim_dir ? (place_on.dir^place_on.dim) : rgen.rand_bool());
	cube_t book;
	vector3d book_scale(book_sz*rgen.rand_uniform(0.8, 1.2), book_sz*rgen.rand_uniform(0.8, 1.2), 0.0);
	float const thickness(book_sz*rgen.rand_uniform(0.1, 0.3));
	book_scale[dim] *= 0.8; // slightly smaller in this dim
	book.set_from_point(point(center.x, center.y, place_on.z2()));
	book.expand_by(book_scale);
	book.z2() += thickness;
	vect_room_object_t &objs(interior->room_geom->objs);

	// check if there's anything in the way; // only handling pens and pencils here; paper is ignored, and larger objects should already be handled
	for (auto i = objs.begin()+objs_start; i != objs.end(); ++i) {
		if (i->type != TYPE_PEN && i->type != TYPE_PENCIL) continue;
		if (!i->intersects(book)) continue;
		set_cube_zvals(book, i->z2(), i->z2()+thickness); // place book on top of object; maybe the book should be tilted?
	}
	colorRGBA const color(book_colors[rgen.rand() % NUM_BOOK_COLORS]);
	objs.emplace_back(book, TYPE_BOOK, room_id, dim, dir, (RO_FLAG_NOCOLL | RO_FLAG_RAND_ROT), tot_light_amt, SHAPE_CUBE, color); // Note: invalidates place_on reference
	set_obj_id(objs);
}

cube_t place_cylin_object(rand_gen_t rgen, cube_t const &place_on, float radius, float height, float dist_from_edge) {
	cube_t c;
	gen_xy_pos_for_round_obj(c, place_on, radius, height, dist_from_edge, rgen); // place at dist_from_edge from edge
	return c;
}

bool building_t::place_bottle_on_obj(rand_gen_t &rgen, cube_t const &place_on, unsigned room_id, float tot_light_amt, cube_t const &avoid) {
	float const window_vspacing(get_window_vspace());
	float const height(window_vspacing*rgen.rand_uniform(0.075, 0.12)), radius(window_vspacing*rgen.rand_uniform(0.012, 0.018));
	if (min(place_on.dx(), place_on.dy()) < 6.0*radius) return 0; // surface is too small to place this bottle
	cube_t const bottle(place_cylin_object(rgen, place_on, radius, height, 2.0*radius));
	if (!avoid.is_all_zeros() && bottle.intersects(avoid)) return 0; // only make one attempt
	vect_room_object_t &objs(interior->room_geom->objs);
	objs.emplace_back(bottle, TYPE_BOTTLE, room_id, 0, 0, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN);
	objs.back().set_as_bottle(rgen.rand(), 3); // 0-3; excludes poison and medicine
	return 1;
}

colorRGBA choose_pot_color(rand_gen_t &rgen) {
	unsigned const num_colors = 8;
	colorRGBA const pot_colors[num_colors] = {LT_GRAY, GRAY, DK_GRAY, BKGRAY, WHITE, LT_BROWN, RED, colorRGBA(1.0, 0.35, 0.18)};
	return pot_colors[rgen.rand() % num_colors];
}
bool building_t::place_plant_on_obj(rand_gen_t &rgen, cube_t const &place_on, unsigned room_id, float tot_light_amt, cube_t const &avoid) {
	float const window_vspacing(get_window_vspace()), height(rgen.rand_uniform(0.25, 0.4)*window_vspacing), max_radius(min(place_on.dx(), place_on.dy())/3.0f);
	vect_room_object_t &objs(interior->room_geom->objs);

	if (building_obj_model_loader.is_model_valid(OBJ_MODEL_PLANT)) { // prefer to place potted plant models, if any exist
		vector3d const sz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_PLANT)); // D, W, H
		float const radius_to_height(0.25*(sz.x + sz.y)/sz.z), radius(min(radius_to_height*height, max_radius)); // cylindrical
		cube_t const plant(place_cylin_object(rgen, place_on, radius, radius/radius_to_height, 1.2*radius)); // recompute height from radius
		
		if (avoid.is_all_zeros() || !plant.intersects(avoid)) { // only make one attempt
			objs.emplace_back(plant, TYPE_PLANT_MODEL, room_id, 0, 0, (RO_FLAG_NOCOLL | RO_FLAG_ADJ_BOT), tot_light_amt, SHAPE_CYLIN, WHITE);
			objs.back().item_flags = rgen.rand(); // choose a random potted plant model if there are more than one
			return 1;
		} // else try to place a non-model plant
	}
	float const radius(min(rgen.rand_uniform(0.06, 0.08)*window_vspacing, max_radius));
	cube_t const plant(place_cylin_object(rgen, place_on, radius, height, 1.2*radius));
	if (!avoid.is_all_zeros() && plant.intersects(avoid)) return 0; // only make one attempt
	objs.emplace_back(plant, TYPE_PLANT, room_id, 0, 0, (RO_FLAG_NOCOLL | RO_FLAG_ADJ_BOT), tot_light_amt, SHAPE_CYLIN, choose_pot_color(rgen));
	set_obj_id(objs);
	return 1;
}

bool building_t::place_laptop_on_obj(rand_gen_t &rgen, room_object_t const &place_on, unsigned room_id, float tot_light_amt, cube_t const &avoid, bool use_dim_dir) {
	point center(place_on.get_cube_center());
	for (unsigned d = 0; d < 2; ++d) {center[d] += 0.1*place_on.get_sz_dim(d)*rgen.rand_uniform(-1.0, 1.0);} // add a slight random shift
	bool const dim(use_dim_dir ? place_on.dim : rgen.rand_bool()), dir(use_dim_dir ? (place_on.dir^place_on.dim^1) : rgen.rand_bool()); // Note: dir is inverted
	float const width(0.136*get_window_vspace());
	vector3d sz;
	sz[!dim] = width;
	sz[ dim] = 0.7*width;  // depth
	sz.z     = 0.06*width; // height
	point const llc(center.x, center.y, place_on.z2());
	cube_t laptop(llc, (llc + sz));
	if (!avoid.is_all_zeros() && laptop.intersects(avoid)) return 0; // only make one attempt
	interior->room_geom->objs.emplace_back(laptop, TYPE_LAPTOP, room_id, dim, dir, (RO_FLAG_NOCOLL | RO_FLAG_RAND_ROT), tot_light_amt); // Note: invalidates place_on reference
	return 1;
}

bool building_t::place_pizza_on_obj(rand_gen_t &rgen, cube_t const &place_on, unsigned room_id, float tot_light_amt, cube_t const &avoid) {
	float const width(0.15*get_window_vspace());
	if (min(place_on.dx(), place_on.dy()) < 1.2*width) return 0; // place_on is too small
	cube_t pizza;
	gen_xy_pos_for_cube_obj(pizza, place_on, vector3d(0.5*width, 0.5*width, 0.0), 0.1*width, rgen);
	bool const dim(rgen.rand_bool()), dir(rgen.rand_bool());
	if (!avoid.is_all_zeros() && pizza.intersects(avoid)) return 0; // only make one attempt
	interior->room_geom->objs.emplace_back(pizza, TYPE_PIZZA_BOX, room_id, dim, dir, (RO_FLAG_NOCOLL | RO_FLAG_RAND_ROT), tot_light_amt); // Note: invalidates place_on reference
	return 1;
}

float get_plate_radius(rand_gen_t &rgen, cube_t const &place_on, float window_vspacing) {
	return min(rgen.rand_uniform(0.05, 0.07)*window_vspacing, 0.25f*min(place_on.dx(), place_on.dy()));
}

bool building_t::place_plate_on_obj(rand_gen_t &rgen, cube_t const &place_on, unsigned room_id, float tot_light_amt, cube_t const &avoid) {
	float const radius(get_plate_radius(rgen, place_on, get_window_vspace()));
	cube_t const plate(place_cylin_object(rgen, place_on, radius, 0.1*radius, 1.1*radius));
	if (!avoid.is_all_zeros() && plate.intersects(avoid)) return 0; // only make one attempt
	vect_room_object_t &objs(interior->room_geom->objs);
	objs.emplace_back(plate, TYPE_PLATE, room_id, 0, 0, RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN);
	set_obj_id(objs);
	return 1;
}

bool building_t::place_cup_on_obj(rand_gen_t &rgen, cube_t const &place_on, unsigned room_id, float tot_light_amt, cube_t const &avoid) {
	if (!building_obj_model_loader.is_model_valid(OBJ_MODEL_CUP)) return 0;
	float const height(0.06*get_window_vspace()), radius(0.5f*height*get_radius_for_square_model(OBJ_MODEL_CUP)); // almost square
	if (min(place_on.dx(), place_on.dy()) < 2.5*radius) return 0; // surface is too small to place this cup
	cube_t const cup(place_cylin_object(rgen, place_on, radius, height, 1.2*radius));
	if (!avoid.is_all_zeros() && cup.intersects(avoid)) return 0; // only make one attempt
	// random dim/dir, plus more randomness on top
	interior->room_geom->objs.emplace_back(cup, TYPE_CUP, room_id, rgen.rand_bool(), rgen.rand_bool(), (RO_FLAG_NOCOLL | RO_FLAG_RAND_ROT), tot_light_amt, SHAPE_CYLIN);
	return 1;
}

bool building_t::place_toy_on_obj(rand_gen_t &rgen, cube_t const &place_on, unsigned room_id, float tot_light_amt, cube_t const &avoid) {
	float const height(0.11*get_window_vspace()), radius(0.5f*height*0.67f);
	if (min(place_on.dx(), place_on.dy()) < 2.5*radius) return 0; // surface is too small to place this toy
	cube_t const toy(place_cylin_object(rgen, place_on, radius, height, 1.1*radius));
	if (!avoid.is_all_zeros() && toy.intersects(avoid)) return 0; // only make one attempt
	interior->room_geom->objs.emplace_back(toy, TYPE_TOY, room_id, rgen.rand_bool(), rgen.rand_bool(), RO_FLAG_NOCOLL, tot_light_amt, SHAPE_CYLIN);
	set_obj_id(interior->room_geom->objs); // used for color selection
	return 1;
}

bool building_t::add_rug_to_room(rand_gen_t rgen, cube_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start) {
	if (!room_object_t::enable_rugs()) return 0; // disabled
	vector3d const room_sz(room.get_size());
	bool const min_dim(room_sz.y < room_sz.x);
	float const ar(rgen.rand_uniform(0.65, 0.85)), length(min(0.7f*room_sz[min_dim]/ar, room_sz[!min_dim]*rgen.rand_uniform(0.4, 0.7))), width(length*ar);
	cube_t rug;
	set_cube_zvals(rug, zval, zval+0.001*get_window_vspace()); // almost flat
	vect_room_object_t &objs(interior->room_geom->objs);
	float sz_scale(1.0);

	for (unsigned n = 0; n < 10; ++n) { // make 10 attempts at choosing a valid alignment
		vector3d center(room.get_cube_center()); // Note: zvals ignored
		bool valid_placement(1);

		for (unsigned d = 0; d < 2; ++d) {
			float const radius(0.5*((bool(d) == min_dim) ? width : length)), scaled_radius(radius*sz_scale);
			center[d] += (0.05f*room_sz[d] + (radius - scaled_radius))*rgen.rand_uniform(-1.0, 1.0); // slight random misalignment, increases with decreasing sz_scale
			rug.d[d][0] = center[d] - radius;
			rug.d[d][1] = center[d] + radius;
		}
		for (auto i = objs.begin() + objs_start; i != objs.end() && valid_placement; ++i) { // check for objects overlapping the rug
			if (i->type == TYPE_FLOORING) continue; // allow placing rugs over flooring
			if (!i->intersects(rug)) continue;

			if (bldg_obj_types[i->type].attached) { // rugs can't overlap these object types; first, see if we can shrink the rug on one side and get it to fit
				float max_area(0.0);
				cube_t best_cand;

				for (unsigned dim = 0; dim < 2; ++dim) {
					for (unsigned dir = 0; dir < 2; ++dir) {
						cube_t cand(rug);
						cand.d[dim][dir] = i->d[dim][!dir] + (dir ? -1.0 : 1.0)*0.025*rug.get_sz_dim(dim); // leave a small gap
						float const area(cand.dx()*cand.dy());
						if (area > max_area) {best_cand = cand; max_area = area;}
					}
				}
				if (max_area > 0.8*rug.dx()*rug.dy()) {rug = best_cand;} // good enough
				else {valid_placement = 0;} // shrink is not enough, try again
			}
			else if (i->type == TYPE_TABLE || i->type == TYPE_DESK || i->type == TYPE_FCABINET) { // rugs can't partially overlap these object types
				valid_placement = rug.contains_cube_xy(*i); // don't expand as that could cause the rug to intersect a previous object
				// maybe beds should be included as well, but then rugs are unlikely to be placed in bedrooms
			}
		} // for i
		if (valid_placement && interior->is_blocked_by_stairs_or_elevator(rug)) {valid_placement = 0;} // check stairs (required for ext basement rooms); no need to check doors

		if (valid_placement) {
			cube_t place_area(room);
			place_area.expand_by_xy(-0.1*get_wall_thickness()); // add small border to avoid alpha blending artifacts if the rug intersects the wall
			rug.intersect_with_cube_xy(place_area); // make sure the rug stays within the room bounds

			if (rug.is_strictly_normalized()) {
				objs.emplace_back(rug, TYPE_RUG, room_id, 0, 0, RO_FLAG_NOCOLL, tot_light_amt);
				room_object_t &rug_obj(objs.back());
				rug_obj.obj_id = uint16_t(objs.size() + 13*room_id + 31*mat_ix); // determines rug texture
				
				// don't use the same texture as a blanket because that looks odd
				for (auto i = objs.begin()+objs_start; i != objs.end(); ++i) {
					if (i->type == TYPE_BLANKET && i->get_rug_tid() == rug_obj.get_rug_tid()) {++rug_obj.obj_id;} // select a different texture
				}
				return 1;
			}
		}
		sz_scale *= 0.9; // decrease rug size and try again
	} // for n
	return 0;
}

// return value: 0=invalid, 1=valid and good, 2=valid but could be better
int building_t::check_valid_picture_placement(room_t const &room, cube_t const &c, float width, float zval, bool dim, bool dir, unsigned objs_start) const {
	assert(interior != nullptr);
	float const wall_thickness(get_wall_thickness()), clearance(4.0*wall_thickness), side_clearance(1.0*wall_thickness);
	cube_t tc(c), keepout(c);
	tc.expand_in_dim(!dim, 0.1*width); // expand slightly to account for frame
	//keepout.z1() = zval; // extend to the floor
	keepout.z1() -= 0.1*c.dz(); // more padding on the bottom
	keepout.d[dim][!dir] += (dir ? -1.0 : 1.0)*clearance;
	keepout.expand_in_dim(!dim, side_clearance); // make sure there's space for the frame
	if (overlaps_other_room_obj(keepout, objs_start, 1)) return 0; // check_all=1, to include outlets, vents, etc.
	bool const inc_open(!is_house && !room.is_office);
	if (is_cube_close_to_doorway(tc, room, 0.0, inc_open)) return 0; // bad placement
	// Note: it's not legal to guard the below check with (room.has_stairs || room.has_elevator) because room.has_stairs may not be set for stack connector stairs that split a wall
	if (interior->is_blocked_by_stairs_or_elevator(tc, 4.0*wall_thickness)) return 0; // check stairs and elevators
	if (!inc_open && !room.is_hallway && is_cube_close_to_doorway(tc, room, 0.0, 1)) return 2; // success, but could be better (doors never open into hallway)

	if (has_complex_floorplan && c.z1() > ground_floor_z1) { // check for office building whiteboards placed on room sides that aren't true walls; skip basements
		cube_t test_cube(c);
		test_cube.expand_by_xy(2.0*wall_thickness); // max sure it extends through the wall
		unsigned num_parts_int(0);

		for (auto p = parts.begin(); p != get_real_parts_end(); ++p) {
			if (p->intersects(test_cube)) {++num_parts_int;}
		}
		assert(num_parts_int > 0);

		if (num_parts_int > 1) { // on the border between two parts, check if there's a wall between them
			cube_t wall_mount(c);
			wall_mount.d[dim][0] = wall_mount.d[dim][1] = c.d[dim][dir] + (dir ? 1.0 : -1.0)*0.5*wall_thickness; // should be in the center of the wall
			bool found_wall(0);

			for (auto const &w: interior->walls[dim]) {
				if (w.contains_cube(wall_mount)) {found_wall = 1; break;}
			}
			if (!found_wall) return 0;
		}
	}
	return 1; // success
}

bool building_t::hang_pictures_in_room(rand_gen_t rgen, room_t const &room, float zval,
	unsigned room_id, float tot_light_amt, unsigned objs_start, unsigned floor_ix, bool is_basement)
{
	if (!room_object_t::enable_pictures()) return 0; // disabled
	
	if (!is_house && !room.is_office) {
		if (room.is_hallway) return 0; // no pictures or whiteboards in office building hallways (what about rooms with stairs?)
		// room in a commercial building - add whiteboard when there is a full wall to use
	}
	if (room.is_sec_bldg) return 0; // no pictures in secondary buildings
	if (room.get_room_type(0) == RTYPE_STORAGE) return 0; // no pictures or whiteboards in storage rooms (always first floor)
	cube_t const &part(get_part_for_room(room));
	float const floor_height(get_window_vspace()), wall_thickness(get_wall_thickness());
	bool const no_ext_walls(!is_basement && (has_windows() || !is_cube())); // don't place on ext walls with windows or non-square orients
	vect_room_object_t &objs(interior->room_geom->objs);
	bool was_hung(0);

	if (!is_house || room.is_office) { // add whiteboards
		if (rgen.rand_float() < 0.1) return 0; // skip 10% of the time
		bool const pref_dim(rgen.rand_bool()), pref_dir(rgen.rand_bool());
		float const floor_thick(get_floor_thickness());

		for (unsigned dim2 = 0; dim2 < 2; ++dim2) {
			for (unsigned dir2 = 0; dir2 < 2; ++dir2) {
				bool const dim(bool(dim2) ^ pref_dim), dir(bool(dir2) ^ pref_dir);
				if (no_ext_walls && fabs(room.d[dim][dir] - part.d[dim][dir]) < 1.1*wall_thickness) continue; // on part boundary, likely ext wall where there may be windows, skip
				cube_t c(room);
				set_cube_zvals(c, zval+0.25*floor_height, zval+0.9*floor_height-floor_thick);
				c.d[dim][!dir] = c.d[dim][dir] + (dir ? -1.0 : 1.0)*0.6*wall_thickness;
				// translate by half wall thickness if not interior hallway or office wall
				if (!(room.inc_half_walls() && classify_room_wall(room, zval, dim, dir, 0) != ROOM_WALL_EXT)) {c.translate_dim(dim, (dir ? 1.0 : -1.0)*0.5*wall_thickness);}
				float const room_len(room.get_sz_dim(!dim));
				c.expand_in_dim(!dim, -0.2*room_len); // xy_space
				float const wb_len(c.get_sz_dim(!dim)), wb_max_len(3.0*floor_height);
				if (wb_len > wb_max_len) {c.expand_in_dim(!dim, -0.5*(wb_len - wb_max_len));} // shrink to max length if needed
				
				if (!check_valid_picture_placement(room, c, 0.6*room_len, zval, dim, dir, objs_start)) { // fails wide/tall placement
					cube_t c_prev(c);
					c.expand_in_dim(!dim, -0.167*c.get_sz_dim(!dim)); // shrink width a bit and try again
					
					if (!check_valid_picture_placement(room, c, 0.4*room_len, zval, dim, dir, objs_start)) { // fails narrow/tall placement
						c = c_prev;
						c.z2() -= 0.15*c.dz(); // shrink height and try again with wide placement
						if (!check_valid_picture_placement(room, c, 0.6*room_len, zval, dim, dir, objs_start)) continue; // give up/fail
					}
				}
				assert(c.is_strictly_normalized());
				objs.emplace_back(c, TYPE_WBOARD, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt); // whiteboard faces dir opposite the wall
				return 1; // done, only need to add one
			} // for dir
		} // for dim
		return 0;
	}
	// add pictures
	for (unsigned dim = 0; dim < 2; ++dim) {
		for (unsigned dir = 0; dir < 2; ++dir) {
			float const wall_pos(room.d[dim][dir]);
			if (no_ext_walls && fabs(room.d[dim][dir] - part.d[dim][dir]) < 1.1*wall_thickness) continue; // on part boundary, likely ext wall where there may be windows, skip
			if (!room.is_hallway && rgen.rand_float() < 0.2) continue; // skip 20% of the time unless it's a hallway
			float const height(floor_height*rgen.rand_uniform(0.3, 0.6)*(is_basement ? 0.8 : 1.0)); // smaller pictures in basement to avoid the pipes
			float const width(height*rgen.rand_uniform(1.5, 2.0)); // width > height
			if (width > 0.8*room.get_sz_dim(!dim)) continue; // not enough space
			float const base_shift((dir ? -1.0 : 1.0)*0.5*wall_thickness); // half a wall's thickness in dir
			point center;
			center[ dim] = wall_pos;
			center[!dim] = room.get_center_dim(!dim);
			center.z     = zval + rgen.rand_uniform(0.45, 0.55)*floor_height; // move up
			float const lo(room.d[!dim][0] + 0.7*width), hi(room.d[!dim][1] - 0.7*width);
			cube_t best_pos;

			for (unsigned n = 0; n < 10; ++n) { // make 10 attempts to choose a position along the wall; first iteration is the center
				if (n > 0) { // try centered first, then non-centered
					if (hi - lo < width) break; // not enough space to shift, can't place this picture
					center[!dim] = rgen.rand_uniform(lo, hi);
				}
				cube_t c(center, center);
				c.expand_in_dim(2, 0.5*height);
				c.d[dim][!dir] += 0.2*base_shift; // move out to prevent z-fighting
				// add an additional half wall thickness for interior hallway and office walls
				if (room.inc_half_walls() && classify_room_wall(room, zval, dim, dir, 0) != ROOM_WALL_EXT) {c.translate_dim(dim, base_shift);}
				c.expand_in_dim(!dim, 0.5*width);
				int const ret(check_valid_picture_placement(room, c, width, zval, dim, dir, objs_start));
				if (ret == 0) continue; // invalid, retry
				best_pos = c;
				if (ret == 1) break; // valid and good - keep this pos
			} // for n
			if (best_pos.is_all_zeros()) continue; // failed placement
			assert(best_pos.is_strictly_normalized());
			objs.emplace_back(best_pos, TYPE_PICTURE, room_id, dim, !dir, RO_FLAG_NOCOLL, tot_light_amt); // picture faces dir opposite the wall
			objs.back().obj_id = uint16_t(objs.size() + 13*room_id + 17*floor_ix + 31*mat_ix + 61*dim + 123*dir); // determines picture texture
			was_hung = 1;
		} // for dir
	} // for dim
	return was_hung;
}

void building_t::add_plants_to_room(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, unsigned num) {
	float const window_vspacing(get_window_vspace());
	cube_t place_area(get_walkable_room_bounds(room));
	place_area.expand_by(-get_trim_thickness()); // shrink to leave a small gap
	zval += 0.01*get_floor_thickness(); // move up slightly to avoid z-fithing of bottom when the dirt is taken
	
	for (unsigned n = 0; n < num; ++n) {
		float const height(rgen.rand_uniform(0.6, 0.9)*window_vspacing), width(rgen.rand_uniform(0.15, 0.35)*window_vspacing);
		vector3d const sz_scale(width/height, width/height, 1.0);
		place_obj_along_wall(TYPE_PLANT, room, height, sz_scale, rgen, zval, room_id, tot_light_amt,
			place_area, objs_start, 0.0, 0, 4, 0, choose_pot_color(rgen), 0, SHAPE_CYLIN); // no clearance, pref_orient, or color
	}
}

void building_t::add_boxes_to_room(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, float tot_light_amt, unsigned objs_start, unsigned max_num) {
	if (max_num == 0) return; // why did we call this?
	float const window_vspacing(get_window_vspace());
	cube_t place_area(get_walkable_room_bounds(room));
	place_area.expand_by(-0.25*get_wall_thickness()); // shrink to leave a small gap for open flaps
	unsigned const num(rgen.rand() % (max_num+1));
	bool const allow_crates(!is_house && room.is_ext_basement()); // backrooms

	for (unsigned n = 0; n < num; ++n) {
		vector3d sz;
		gen_crate_sz(sz, rgen, window_vspacing);
		sz *= 1.5; // make larger than storage room boxes
		room_object const type((allow_crates && rgen.rand_bool()) ? TYPE_CRATE : TYPE_BOX);
		place_obj_along_wall(type, room, sz.z, sz, rgen, zval, room_id, tot_light_amt, place_area, objs_start, 0.0, 0, 4, 0, gen_box_color(rgen));
	} // for n
}

room_object_t get_conduit(bool dim, bool dir, float radius, float wall_pos_dim, float wall_pos_not_dim, float z1, float z2, unsigned room_id) {
	cube_t conduit;
	set_wall_width(conduit, wall_pos_not_dim, radius, !dim);
	conduit.d[dim][ dir] = wall_pos_dim; // flush with wall
	conduit.d[dim][!dir] = conduit.d[dim][dir] + (dir ? -1.0 : 1.0)*2.0*radius;
	set_cube_zvals(conduit, z1, z2);
	return room_object_t(conduit, TYPE_PIPE, room_id, 0, 1, RO_FLAG_NOCOLL, 1.0, SHAPE_CYLIN, LT_GRAY); // vertical
}

void building_t::add_light_switches_to_room(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, unsigned objs_start, bool is_ground_floor, bool is_basement) {
	float const floor_spacing(get_window_vspace()), wall_thickness(get_wall_thickness()), switch_thickness(0.2*wall_thickness);
	float const switch_height(1.8*wall_thickness), switch_hwidth(0.5*wall_thickness), min_wall_spacing(switch_hwidth + 2.0*wall_thickness);
	cube_t const room_bounds(get_walkable_room_bounds(room));
	if (min(room_bounds.dx(), room_bounds.dy()) < 8.0*switch_hwidth) return; // room is too small; shouldn't happen
	vect_door_stack_t &doorways(get_doorways_for_room(room, zval)); // place light switch next to a door
	if (doorways.size() > 1 && rgen.rand_bool()) {std::reverse(doorways.begin(), doorways.end());} // random permute if more than 2 doorways?
	vect_room_object_t &objs(interior->room_geom->objs);
	unsigned const objs_end(objs.size());
	bool const first_side(rgen.rand_bool());
	vect_door_stack_t ext_doors; // not really door stacks, but we can fill in the data to treat them as such
	cube_t c;
	c.z1() = zval + 0.38*floor_spacing; // same for every switch
	c.z2() = c.z1() + switch_height;

	if (is_ground_floor) { // handle exterior doors
		cube_t room_exp(room);
		room_exp.expand_by(wall_thickness, wall_thickness, -wall_thickness); // expand in XY and shrink in Z

		for (auto d = doors.begin(); d != doors.end(); ++d) {
			if (!d->is_exterior_door() || d->type == tquad_with_ix_t::TYPE_RDOOR) continue;
			cube_t bc(d->get_bcube());
			if (!room_exp.contains_pt(bc.get_cube_center())) continue;
			bool const dim(bc.dy() < bc.dx());
			bc.expand_in_dim(dim, 0.4*wall_thickness); // expand slightly to make it nonzero area
			ext_doors.emplace_back(door_t(bc, dim, 0), 0); // dir=0, first_door_ix=0 because it's unused
		}
	}
	for (unsigned ei = 0; ei < 2; ++ei) { // exterior, interior
		vect_door_stack_t const &cands(ei ? doorways : ext_doors);
		unsigned const max_ls(is_house ? 2 : 1); // place up to 2 light switches in this room if it's a house, otherwise place only 1
		unsigned num_ls(0);

		for (auto i = cands.begin(); i != cands.end() && num_ls < max_ls; ++i) {
			if (!is_house && room.is_ext_basement() && room_bounds.contains_cube_xy(*i)) continue; // skip interior backrooms doors
			// check for windows if (real_num_parts > 1)? is it actually possible for doors to be within far_spacing of a window?
			bool const dim(i->dim), dir(i->get_center_dim(dim) > room.get_center_dim(dim));
			float const dir_sign(dir ? -1.0 : 1.0), door_width(i->get_width()), near_spacing(0.25*door_width), far_spacing(1.25*door_width); // off to side of door when open
			assert(door_width > 0.0);
			cube_t const &wall_bounds(ei ? room_bounds : room); // exterior door should use the original room, not room_bounds
			c.d[dim][ dir] = wall_bounds.d[dim][dir]; // flush with wall
			c.d[dim][!dir] = c.d[dim][dir] + dir_sign*switch_thickness; // expand out a bit
			bool done(0);

			for (unsigned Side = 0; Side < 2 && !done; ++Side) { // try both sides of the doorway
				bool const side(bool(Side) ^ first_side);

				for (unsigned nf = 0; nf < 2; ++nf) { // {near, far}
					float const spacing(nf ? far_spacing : near_spacing), wall_pos(i->d[!dim][side] + (side ? 1.0 : -1.0)*spacing);
					if (wall_pos < room_bounds.d[!dim][0] + min_wall_spacing || wall_pos > room_bounds.d[!dim][1] - min_wall_spacing) continue; // too close to the adjacent wall
					set_wall_width(c, wall_pos, switch_hwidth, !dim);
					cube_t c_test(c);
					c_test.d[dim][!dir] += dir_sign*wall_thickness; // expand out more so that it's guaranteed to intersect appliances placed near the wall
					if (overlaps_other_room_obj(c_test, objs_start))          continue;
					if (is_obj_placement_blocked(c, room, (ei==1), 1))        continue; // inc_open_doors=1/check_open_dir=1 for inside, to avoid placing behind an open door
					if (!check_if_placed_on_interior_wall(c, room, dim, dir)) continue; // ensure the switch is on a wall
					// if is_basement, and this is an exterior wall, use a non-recessed light switch? but the basement ext wall will never have a doorway; next to basement stairs?
					unsigned flags(RO_FLAG_NOCOLL);

					if (is_house && is_basement && classify_room_wall(room, zval, dim, dir, 0) == ROOM_WALL_EXT) { // house exterior basement wall; non-recessed
						room_object_t const conduit(get_conduit(dim, dir, 0.25*switch_hwidth, c.d[dim][dir], wall_pos, c.z2(), (zval + get_floor_ceil_gap()), room_id));

						if (!overlaps_other_room_obj(conduit, objs_start)) {
							objs.push_back(conduit);
							c.d[dim][!dir] += dir_sign*1.0*switch_hwidth; // shift front outward more
							flags |= RO_FLAG_HANGING;
						}
					}
					expand_to_nonzero_area(c, switch_thickness, dim);
					objs.emplace_back(c, TYPE_SWITCH, room_id, dim, dir, flags, 1.0); // dim/dir matches wall; fully lit
					done = 1; // done, only need to add one for this door
					++num_ls;
					break;
				} // for nf
			} // for side
		} // for i
	} // for ei
	if (!is_house || is_basement) return; // no closets - done

	// add closet light switches
	for (unsigned i = objs_start; i < objs_end; ++i) { // can't iterate over objs while modifying it
		room_object_t const &obj(objs[i]);
		if (obj.type != TYPE_CLOSET) continue;
		cube_t cubes[5]; // front left, left side, front right, right side, door
		get_closet_cubes(obj, cubes); // for_collision=0
		bool const dim(obj.dim), dir(!obj.dir);
		bool side_of_door(0);
		if (obj.is_small_closet()) {side_of_door = 1;} // same side as door handle
		else { // large closet, put the light switch on the side closer to the center of the room
			float const room_center(room.get_center_dim(!dim));
			side_of_door = (fabs(cubes[2].get_center_dim(!dim) - room_center) < fabs(cubes[0].get_center_dim(!dim) - room_center));
		}
		cube_t const &target_wall(cubes[2*side_of_door]); // front left or front right
		c.d[dim][ dir] = target_wall.d[dim][!dir]; // flush with wall
		c.d[dim][!dir] = c.d[dim][dir] + (dir ? -1.0 : 1.0)*switch_thickness; // expand out a bit
		set_wall_width(c, target_wall.get_center_dim(!dim), switch_hwidth, !dim);
		expand_to_nonzero_area(c, switch_thickness, dim);
		// since nothing is placed against the exterior wall of the closet near the door (to avoid blocking it), we don't need to check for collisions with room objects
		objs.emplace_back(c, TYPE_SWITCH, room_id, dim, dir, (RO_FLAG_NOCOLL | RO_FLAG_IN_CLOSET), 1.0); // dim/dir matches wall; fully lit; flag for closet
		//break; // there can be only one closet per room; done (unless I add multiple closets later?)
	} // for i
}

void building_t::add_outlets_to_room(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, unsigned objs_start, bool is_ground_floor, bool is_basement) {
	float const wall_thickness(get_wall_thickness());
	float const plate_thickness(0.03*wall_thickness), plate_height(1.8*wall_thickness), plate_hwidth(0.5*wall_thickness), min_wall_spacing(4.0*plate_hwidth);
	cube_t const room_bounds(get_walkable_room_bounds(room));
	if (min(room_bounds.dx(), room_bounds.dy()) < 3.0*min_wall_spacing) return; // room is too small; shouldn't happen
	vect_door_stack_t const &doorways(get_doorways_for_room(room, zval));
	vect_room_object_t &objs(interior->room_geom->objs);
	cube_t c;
	c.z1() = zval + get_trim_height() + 0.4*plate_height; // wall trim height + some extra padding; same for every outlet
	c.z2() = c.z1() + plate_height;

	// try to add an outlet to each wall, down near the floor so that they don't intersect objects such as pictures
	for (unsigned wall = 0; wall < 4; ++wall) {
		bool const dim(wall >> 1), dir(wall & 1);
		if (!is_house && room.get_sz_dim(!dim) < room.get_sz_dim(dim)) continue; // only add outlets to the long walls of office building rooms
		bool const is_exterior_wall(classify_room_wall(room, zval, dim, dir, 0) == ROOM_WALL_EXT); // includes basement
		if (is_exterior_wall && !is_cube()) continue; // don't place on ext wall if it's not X/Y aligned
		cube_t const &wall_bounds(is_exterior_wall ? room : room_bounds); // exterior wall should use the original room, not room_bounds
		float const wall_pos(rgen.rand_uniform((room_bounds.d[!dim][0] + min_wall_spacing), (room_bounds.d[!dim][1] - min_wall_spacing)));
		float const wall_face(wall_bounds.d[dim][dir]), dir_sign(dir ? -1.0 : 1.0);
		c.d[dim][ dir] = wall_face; // flush with wall
		c.d[dim][!dir] = wall_face + dir_sign*plate_thickness; // expand out a bit
		set_wall_width(c, wall_pos, plate_hwidth, !dim);

		if (!is_basement && has_windows() && is_exterior_wall) { // check for window intersection
			cube_t const part(get_part_for_room(room));
			float const window_hspacing(get_hspacing_for_part(part, !dim)), window_h_border(get_window_h_border());
			// expand by the width of the window trim, plus some padded wall plate width, then check to the left and right;
			// 2*xy_expand should be smaller than a window so we can't have a window fit in between the left and right sides
			float const xy_expand(get_trim_thickness() + 1.2f*plate_hwidth);
			if (is_val_inside_window(part, !dim, (wall_pos - xy_expand), window_hspacing, window_h_border) ||
				is_val_inside_window(part, !dim, (wall_pos + xy_expand), window_hspacing, window_h_border)) continue;
		}
		cube_t c_exp(c);
		c_exp.expand_by_xy(0.5*wall_thickness);
		if (overlaps_other_room_obj(c_exp, objs_start, 1))     continue; // check for things like closets; check_all=1 to include blinds
		if (interior->is_blocked_by_stairs_or_elevator(c_exp)) continue; // check stairs and elevators
		if (!check_cube_within_part_sides(c_exp))              continue; // handle non-cube buildings
		bool bad_place(0);

		if (is_ground_floor) { // handle exterior doors
			for (auto d = doors.begin(); d != doors.end(); ++d) {
				if (!d->is_exterior_door() || d->type == tquad_with_ix_t::TYPE_RDOOR) continue;
				cube_t bc(d->get_bcube());
				bc.expand_in_dim(dim, wall_thickness); // make sure it's nonzero area
				if (bc.intersects(c_exp)) {bad_place = 1; break;}
			}
			if (bad_place) continue;
		}
		for (auto const &d : doorways) {
			if (d.get_true_bcube().intersects(c_exp)) {bad_place = 1; break;}
		}
		if (bad_place) continue;
		if (!check_if_placed_on_interior_wall(c, room, dim, dir)) continue; // ensure the outlet is on a wall
		unsigned flags(RO_FLAG_NOCOLL);

		if (is_house && is_basement && is_exterior_wall) { // house exterior basement wall; non-recessed
			room_object_t const conduit(get_conduit(dim, dir, 0.25*plate_hwidth, c.d[dim][dir], wall_pos, c.z2(), (zval + get_floor_ceil_gap()), room_id));

			if (!overlaps_other_room_obj(conduit, objs_start)) {
				objs.push_back(conduit);
				c.d[dim][!dir] += dir_sign*1.2*plate_hwidth; // shift front outward more
				flags |= RO_FLAG_HANGING;
			}
		}
		expand_to_nonzero_area(c, plate_thickness, dim);
		objs.emplace_back(c, TYPE_OUTLET, room_id, dim, dir, flags, 1.0); // dim/dir matches wall; fully lit
	} // for wall
}

cube_t door_base_t::get_open_door_bcube_for_room(cube_t const &room) const {
	bool const dir(get_check_dirs());
	cube_t bcube(get_true_bcube());
	if (door_opens_inward(*this, room)) {bcube.d[!dim][dir] += (dir ? 1.0 : -1.0)*get_width();} // include door fully open position
	return bcube;
}

bool building_t::add_wall_vent_to_room(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, unsigned objs_start, bool check_for_ducts) {
	float const wall_thickness(get_wall_thickness()), ceiling_zval(zval + get_floor_ceil_gap());
	float const thickness(0.1*wall_thickness), height(2.5*wall_thickness), hwidth(2.0*wall_thickness), min_wall_spacing(1.5*hwidth);
	cube_t const room_bounds(get_walkable_room_bounds(room));
	if (min(room_bounds.dx(), room_bounds.dy()) < 3.0*min_wall_spacing) return 0; // room is too small; shouldn't happen
	bool const pref_dim(room.dx() < room.dy()); // shorter dim, to make it less likely to conflict with whiteboards
	vect_door_stack_t const &doorways(get_doorways_for_room(room, zval));
	vect_room_object_t &objs(interior->room_geom->objs);
	cube_t c;
	c.z2() = ceiling_zval - 0.1*height;
	c.z1() = c.z2() - height;

	for (unsigned n = 0; n < 100; ++n) { // 100 tries
		bool const dim((n < 10) ? pref_dim : rgen.rand_bool()), dir(rgen.rand_bool());
		if (classify_room_wall(room, zval, dim, dir, 0) == ROOM_WALL_EXT) continue; // skip exterior walls
		float const wall_pos(rgen.rand_uniform((room_bounds.d[!dim][0] + min_wall_spacing), (room_bounds.d[!dim][1] - min_wall_spacing)));
		float const wall_face(room_bounds.d[dim][dir]);
		c.d[dim][ dir] = wall_face; // flush with wall
		c.d[dim][!dir] = wall_face + (dir ? -1.0 : 1.0)*thickness; // expand out a bit
		set_wall_width(c, wall_pos, hwidth, !dim);
		cube_t c_exp(c);
		c_exp.expand_by_xy(0.5*wall_thickness);
		c_exp.d[dim][!dir] += (dir ? -1.0 : 1.0)*hwidth; // add some clearance in front
		if (overlaps_other_room_obj(c_exp, objs_start, 1))     continue; // check for objects; check_all=1 to inc whiteboards; excludes picture frames
		if (interior->is_blocked_by_stairs_or_elevator(c_exp)) continue; // check stairs and elevators
		cube_t door_test_cube(c_exp);
		door_test_cube.expand_in_dim(!dim, 0.25*hwidth); // not too close to doors
		bool bad_place(0);

		for (auto const &d : doorways) {
			if (d.get_open_door_bcube_for_room(room).intersects(door_test_cube)) {bad_place = 1; break;}
		}
		if (bad_place) continue;
		if (!check_if_placed_on_interior_wall(c, room, dim, dir)) continue; // ensure the vent is on a wall; is this really needed?
		if (!check_cube_within_part_sides(c)) continue; // handle non-cube buildings

		if (check_for_ducts) { // if this is a utility room, check to see if we can connect the vent to a furnace with a duct
			assert(objs_start <= objs.size());

			for (auto i = objs.begin()+objs_start; i != objs.end(); ++i) {
				//if (i->type == TYPE_DUCT) {} // maybe this can be implemented once we add ducts to rooms (rather than only attics)
				if (i->type != TYPE_FURNACE) continue;
				if (i->dim != dim || i->dir == dir) continue; // wrong wall
				bool const side(i->get_center_dim(!dim) < c.get_center_dim(!dim));
				float const duct_wall_shift((dir ? -1.0 : 1.0)*0.6*height), duct_end_shift((side ? -1.0 : 1.0)*wall_thickness);
				cube_t duct(c);
				duct.d[ dim][!dir ] = wall_face + duct_wall_shift; // expand outward
				duct.d[!dim][!side] = i->d[!dim][side]; // flush with the side of the furnace
				cube_t test_cube(duct);
				test_cube.d[!dim][!side] -= duct_end_shift; // shrink slightly so as not to overlap the furnace
				duct     .d[!dim][!side] += duct_end_shift; // extend duct into furnace duct (since there's a gap on the edge of the furnace)
				if (overlaps_other_room_obj(test_cube, objs_start, 1)) continue; // check for objects
				if (is_obj_placement_blocked(duct, room, 1))           continue; // too close to a doorway/stairs/elevator; inc_open=1
				objs.emplace_back(duct, TYPE_DUCT, room_id, !dim, 0, RO_FLAG_NOCOLL, 1.0, SHAPE_CUBE, DUCT_COLOR);
				c.translate_dim(dim, duct_wall_shift); // place vent on the duct
				break; // only connect to one furnace
			} // for i
		}
		objs.emplace_back(c, TYPE_VENT, room_id, dim, dir, RO_FLAG_NOCOLL, 1.0); // dim/dir matches wall; fully lit
		return 1; // done
	} // for n
	return 0; // failed
}

// what about floor vent?
bool building_t::add_ceil_vent_to_room(rand_gen_t rgen, room_t const &room, float zval, unsigned room_id, unsigned objs_start) {
	float const wall_thickness(get_wall_thickness()), ceiling_zval(zval + get_floor_ceil_gap());
	float const thickness(0.1*wall_thickness), hlen(2.0*wall_thickness), hwid(1.25*wall_thickness);
	cube_t const room_bounds(get_walkable_room_bounds(room));
	if (min(room_bounds.dx(), room_bounds.dy()) < 4.0*hlen) return 0; // room is too small; shouldn't happen
	cube_t c, attic_access;
	set_cube_zvals(c, ceiling_zval-thickness, ceiling_zval);

	if (has_attic()) {
		attic_access = interior->attic_access;
		attic_access.z1() -= get_floor_thickness(); // expand downward a bit do that it would intersect a vent below
	}
	for (unsigned n = 0; n < 10; ++n) { // 10 tries
		bool const dim(rgen.rand_bool());
		point sz;
		sz[ dim] = hlen;
		sz[!dim] = hwid;
		point const center(gen_xy_pos_in_area(room_bounds, sz, rgen));
		set_wall_width(c, center[ dim], hlen,  dim);
		set_wall_width(c, center[!dim], hwid, !dim);
		cube_t c_exp(c);
		c_exp.expand_by_xy(0.5*wall_thickness); // add a bit of padding
		if (overlaps_other_room_obj(c_exp, objs_start, 1))     continue; // check for things like closets; check_all=1 to inc whiteboards; excludes picture frames
		if (interior->is_blocked_by_stairs_or_elevator(c_exp)) continue; // check stairs and elevators
		if (is_cube_close_to_doorway(c, room, 0.0, 1, 1))      continue;
		if (vent_in_attic_test(c, dim) == 2)                   continue; // not enough clearance in attic for duct
		if (has_attic() && c.intersects(attic_access))         continue; // check attic access door
		interior->room_geom->objs.emplace_back(c, TYPE_VENT, room_id, dim, 0, (RO_FLAG_NOCOLL | RO_FLAG_HANGING), 1.0); // dir=0; fully lit
		return 1; // done
	} // for n
	return 0; // failed
}

bool building_t::check_if_placed_on_interior_wall(cube_t const &c, room_t const &room, bool dim, bool dir) const {
	if (!has_small_part && (is_house || !room.is_hallway)) return 1; // check not needed in this case, any non-door location is a wall
	float const wall_thickness(get_wall_thickness()), wall_face(c.d[dim][dir]);
	cube_t test_cube(c);
	test_cube.d[dim][0] = test_cube.d[dim][1] = wall_face - (dir ? -1.0 : 1.0)*0.5*wall_thickness; // move inward
	test_cube.expand_in_dim(!dim, 0.5*wall_thickness);
	// check for exterior wall
	bool intersects_part(0);

	for (auto p = parts.begin(); p != get_real_parts_end(); ++p) {
		if (p->intersects(test_cube)) {intersects_part = 1; break;}
	}
	if (!intersects_part) return 1; // not contained in a part, must be an exterior wall
	// check for interior wall
	for (auto const &w : interior->walls[dim]) {
		if (w.contains_cube(test_cube)) return 1;
	}
	return 0;
}

bool building_t::place_eating_items_on_table(rand_gen_t &rgen, unsigned table_obj_id) {
	vect_room_object_t &objs(interior->room_geom->objs);
	assert(table_obj_id < objs.size());
	room_object_t const table(objs[table_obj_id]); // deep copy to avoid invalidating the reference
	float const floor_spacing(get_window_vspace()), plate_radius(get_plate_radius(rgen, table, floor_spacing)), plate_height(0.1*plate_radius), spacing(1.33*plate_radius);
	unsigned const objs_size(objs.size());
	bool added_obj(0);

	for (unsigned i = (table_obj_id + 1); i < objs_size; ++i) { // iterate over chairs (by index, since we're adding to objs)
		if (objs[i].type != TYPE_CHAIR) break; // done with chairs for this table
		point const chair_center(objs[i].get_cube_center()), table_center(table.get_cube_center());
		point pos;

		if (table.shape == SHAPE_CYLIN) { // circular
			float const dist(table.get_radius() - spacing);
			pos = table_center + dist*(chair_center - table_center).get_norm();
		}
		else { // rectangular
			cube_t place_bounds(table);
			place_bounds.expand_by_xy(-spacing);
			pos = place_bounds.closest_pt(chair_center);
		}
		cube_t plate;
		plate.set_from_sphere(pos, plate_radius);
		set_cube_zvals(plate, table.z2(), table.z2()+plate_height); // place on the table
		objs.emplace_back(plate, TYPE_PLATE, table.room_id, 0, 0, RO_FLAG_NOCOLL, table.light_amt, SHAPE_CYLIN);
		set_obj_id(objs);

		if (building_obj_model_loader.is_model_valid(OBJ_MODEL_SILVER)) {
			vector3d const sz(building_obj_model_loader.get_model_world_space_size(OBJ_MODEL_SILVER)); // D, W, H
			float const sw_height(0.0075*floor_spacing), sw_hwidth(0.5*sw_height*sz.x/sz.z), sw_hlen(0.5*sw_height*sz.y/sz.z); // Note: x/y swapped
			vector3d const offset(pos - table_center);
			bool const dim(fabs(offset.x) < fabs(offset.y)), dir(offset[dim] > 0.0);
			cube_t sw_bc;
			set_cube_zvals(sw_bc, table.z2()+0.1*sw_height, table.z2()+sw_height);
			set_wall_width(sw_bc, pos[!dim] + ((dim ^ dir) ? 1.0 : -1.0)*1.2*(plate_radius + sw_hlen), sw_hlen, !dim);
			set_wall_width(sw_bc, pos[ dim], sw_hwidth, dim);
			objs.emplace_back(sw_bc, TYPE_SILVER, table.room_id, dim, dir, RO_FLAG_NOCOLL, table.light_amt, SHAPE_CUBE, GRAY);
		}
		added_obj = 1;
	} // for i
	if (added_obj) { // place a vase in the center of the table
		float const vase_radius(rgen.rand_uniform(0.35, 0.6)*plate_radius), vase_height(rgen.rand_uniform(2.0, 6.0)*vase_radius);
		cube_t vase;
		vase.set_from_sphere(table.get_cube_center(), vase_radius);
		set_cube_zvals(vase, table.z2(), table.z2()+vase_height); // place on the table
		objs.emplace_back(vase, TYPE_VASE, table.room_id, 0, 0, RO_FLAG_NOCOLL, table.light_amt, SHAPE_CYLIN, gen_vase_color(rgen));
		set_obj_id(objs);
	}
	return added_obj;
}

void building_t::place_objects_onto_surfaces(rand_gen_t rgen, room_t const &room, unsigned room_id, float tot_light_amt, unsigned objs_start, unsigned floor, bool is_basement) {
	if (room.is_hallway) return; // no objects placed in hallways, but there shouldn't be any surfaces either (except for reception desk?)
	vect_room_object_t &objs(interior->room_geom->objs);
	assert(objs.size() > objs_start);
	bool const is_library(room.get_room_type(floor) == RTYPE_LIBRARY);
	bool const is_kitchen(room.get_room_type(floor) == RTYPE_KITCHEN);
	bool const sparse_place(floor > 0 && interior->rooms.size() > 40); // fewer objects on upper floors of large office buildings as an optimization
	float const place_book_prob(( is_house ? 1.0 : 0.5)*(room.is_office ? 0.80 : 1.00)*(sparse_place ? 0.75 : 1.0));
	float const place_bottle_prob(is_house ? 1.0 :      (room.is_office ? 0.80 : 0.50)*(sparse_place ? 0.50 : 1.0));
	float const place_cup_prob   (is_house ? 1.0 :      (room.is_office ? 0.50 : 0.25)*(sparse_place ? 0.50 : 1.0));
	float const place_plant_prob (is_house ? 1.0 :      (room.is_office ? 0.25 : 0.15)*(sparse_place ? 0.75 : 1.0));
	float const place_laptop_prob(is_house ? 0.4 :      (room.is_office ? 0.60 : 0.50)*(sparse_place ? 0.80 : 1.0));
	float const place_pizza_prob (is_house ? 1.0 :      (room.is_office ? 0.30 : 0.15)*(sparse_place ? 0.75 : 1.0));
	unsigned const objs_end(objs.size());
	bool placed_book_on_counter(0);

	// see if we can place objects on any room object top surfaces
	for (unsigned i = objs_start; i < objs_end; ++i) { // can't iterate over objs because we modify it
		room_object_t const &obj(objs[i]);
		// add place settings to kitchen and dining room tables 50% of the time
		bool const is_table(obj.type == TYPE_TABLE);
		bool const is_eating_table(is_table && (room.get_room_type(floor) == RTYPE_KITCHEN || room.get_room_type(floor) == RTYPE_DINING) && rgen.rand_bool());
		if (is_eating_table && place_eating_items_on_table(rgen, i)) continue; // no other items to place
		float book_prob(0.0), bottle_prob(0.0), cup_prob(0.0), plant_prob(0.0), laptop_prob(0.0), pizza_prob(0.0), toy_prob(0.0);
		cube_t avoid;

		if (obj.type == TYPE_TABLE && i == objs_start) { // only first table (not TV table)
			book_prob   = 0.4*place_book_prob;
			bottle_prob = 0.6*place_bottle_prob;
			cup_prob    = 0.5*place_cup_prob;
			plant_prob  = 0.6*place_plant_prob;
			laptop_prob = 0.3*place_laptop_prob;
			pizza_prob  = 0.8*place_pizza_prob;
			if (is_house) {toy_prob = 0.5;} // toys are in houses only
		}
		else if (obj.type == TYPE_DESK && (i+1 == objs_end || objs[i+1].type != TYPE_MONITOR)) { // desk with no computer monitor
			book_prob   = 0.8*place_book_prob;
			bottle_prob = 0.4*place_bottle_prob;
			cup_prob    = 0.3*place_cup_prob;
			plant_prob  = 0.3*place_plant_prob;
			laptop_prob = 0.7*place_laptop_prob;
			pizza_prob  = 0.4*place_pizza_prob;
		}
		else if (obj.type == TYPE_COUNTER && !(obj.flags & RO_FLAG_ADJ_TOP)) { // counter without a microwave
			book_prob   = (placed_book_on_counter ? 0.0 : 0.5); // only place one book per counter
			bottle_prob = 0.25*place_bottle_prob;
			cup_prob    = 0.30*place_cup_prob;
			plant_prob  = 0.10*place_plant_prob;
			laptop_prob = 0.05*place_laptop_prob;
			pizza_prob  = 0.50*place_pizza_prob;
		}
		else if ((obj.type == TYPE_DRESSER || obj.type == TYPE_NIGHTSTAND) && !(obj.flags & RO_FLAG_ADJ_TOP)) { // dresser or nightstand with nothing on it yet; no pizza
			book_prob   = 0.25*place_book_prob;
			bottle_prob = 0.15*place_bottle_prob;
			cup_prob    = 0.15*place_cup_prob;
			plant_prob  = 0.1*place_plant_prob;
			laptop_prob = 0.1*place_laptop_prob;
			toy_prob    = 0.15;
		}
		else {
			continue;
		}
		if (is_library) {book_prob *= 2.5;} // higher probability of books placed in a library
		if (is_kitchen) {cup_prob  *= 2.0; pizza_prob *= 2.0;} // higher probability of cups and pizza if placed in a kitchen
		room_object_t surface(obj); // deep copy to allow modification and avoid using an invalidated reference
		
		if (obj.shape == SHAPE_CYLIN) { // find max contained XY rectangle (simpler than testing distance to center vs. radius)
			for (unsigned d = 0; d < 2; ++d) {surface.expand_in_dim(d, -0.5*(1.0 - SQRTOFTWOINV)*surface.get_sz_dim(d));}
		}
		if (is_eating_table) { // table in a room for eating, add a plate
			if (place_plate_on_obj(rgen, surface, room_id, tot_light_amt, avoid)) {avoid = objs.back();}
		}
		if (avoid.is_all_zeros() && rgen.rand_probability(book_prob)) { // place book if it's the first item (no plate)
			placed_book_on_counter |= (obj.type == TYPE_COUNTER);
			place_book_on_obj(rgen, surface, room_id, tot_light_amt, objs_start, !is_table);
			avoid = objs.back();
		}
		if (avoid.is_all_zeros() && obj.type == TYPE_DESK) {
			// if we have no other avoid object, and this is a desk, try to avoid placing an object that overlaps a pen or pencil
			for (unsigned j = i+1; j < objs_end; ++j) {
				room_object_t const &obj2(objs[j]);
				if (obj2.type == TYPE_PEN || obj2.type == TYPE_PENCIL) {avoid = obj2; break;} // we can only use the first one
			}
		}
		unsigned const num_obj_types = 6;
		unsigned const obj_type_start(rgen.rand() % num_obj_types); // select a random starting point to remove bias toward objects checked first
		bool placed(0);

		for (unsigned n = 0; n < num_obj_types && !placed; ++n) { // place a single object; ***Note: obj is invalidated by this loop and can't be used***
			switch ((n + obj_type_start) % num_obj_types) {
			case 0: placed = (rgen.rand_probability(bottle_prob) && place_bottle_on_obj(rgen, surface, room_id, tot_light_amt, avoid)); break;
			case 1: placed = (rgen.rand_probability(cup_prob   ) && place_cup_on_obj   (rgen, surface, room_id, tot_light_amt, avoid)); break;
			case 2: placed = (rgen.rand_probability(laptop_prob) && place_laptop_on_obj(rgen, surface, room_id, tot_light_amt, avoid, !is_table)); break;
			case 3: placed = (rgen.rand_probability(pizza_prob ) && place_pizza_on_obj (rgen, surface, room_id, tot_light_amt, avoid)); break;
			case 4: placed = (!is_basement && rgen.rand_probability(plant_prob) && place_plant_on_obj(rgen, surface, room_id, tot_light_amt, avoid)); break;
			case 5: placed = (rgen.rand_probability(toy_prob)    && place_toy_on_obj   (rgen, surface, room_id, tot_light_amt, avoid)); break;
			}
		} // for n
	} // for i
}

template<typename T> bool any_cube_contains(cube_t const &cube, T const &cubes) {
	for (auto const &c : cubes) {if (c.contains_cube(cube)) return 1;}
	return 0;
}
bool building_t::is_light_placement_valid(cube_t const &light, cube_t const &room, float pad) const {
	cube_t light_ext(light);
	light_ext.expand_by_xy(pad);
	if (!room.contains_cube(light_ext))            return 0; // room too small?
	if (has_bcube_int(light, interior->elevators)) return 0;
	if (!check_cube_within_part_sides(light))      return 0; // handle non-cube buildings
	unsigned const pg_wall_start(interior->room_geom->wall_ps_start);

	// check for intersection with low pipes such as sprinkler pipes that have been previously placed; only works for top level of parking garage
	if (light.z1() < ground_floor_z1 && has_parking_garage && pg_wall_start > 0) {
		vect_room_object_t const &objs(interior->room_geom->objs);
		assert(pg_wall_start < objs.size());

		for (auto i = (objs.begin() + pg_wall_start); i != objs.end(); ++i) {
			if (i->type == TYPE_PIPE && i->intersects(light)) return 0; // check for pipe intersections (in particular horizontal sprinkler pipes)
		}
	}
	light_ext.z1() = light_ext.z1() = light.z2() + get_fc_thickness(); // shift in between the ceiling and floor so that we can do a cube contains check
	if (any_cube_contains(light_ext, interior->fc_occluders)) return 1; // Note: don't need to check skylights because fc_occluders excludes skylights
	if (PLACE_LIGHTS_ON_SKYLIGHTS && any_cube_contains(light_ext, skylights)) return 1; // place on a skylight
	return 0;
}

void building_t::try_place_light_on_ceiling(cube_t const &light, room_t const &room, bool room_dim, float pad, bool allow_rot, bool allow_mult,
	unsigned nx, unsigned ny, unsigned check_coll_start, vect_cube_t &lights, rand_gen_t &rgen) const
{
	assert(has_room_geom());
	float const window_vspacing(get_window_vspace());
	int light_placed(0); // 0=no, 1=at center, 2=at alternate location

	if (is_light_placement_valid(light, room, pad) && !overlaps_other_room_obj(light, check_coll_start)) {
		lights.push_back(light); // valid placement, done
		light_placed = 1;
	}
	else {
		point room_center(room.get_cube_center());
		bool const first_dir(rgen.rand_bool()); // randomize shift direction
		cube_t light_cand(light); // Note: same logic for cube and cylinder light shape - cylinder uses bounding cube
		unsigned const num_shifts = 10;

		if (allow_rot) { // flip aspect ratio
			float const sz_diff(0.5*(light.dx() - light.dy()));
			light_cand.expand_in_dim(0, -sz_diff);
			light_cand.expand_in_dim(1,  sz_diff);
		}
		for (unsigned D = 0; D < 2 && !light_placed; ++D) { // try both dims
			bool const dim(room_dim ^ bool(D));
			unsigned const num(room_dim ? ny : nx);
			float const shift_step((0.5*(room.get_sz_dim(dim) - light_cand.get_sz_dim(dim)))/(num*num_shifts)); // shift within the bounds of placement grid based on num

			for (unsigned d = 0; d < 2; ++d) { // dir: see if we can place it by moving on one direction
				for (unsigned n = 1; n <= num_shifts; ++n) { // try different shift values
					cube_t cand(light_cand);
					cand.translate_dim(dim, ((bool(d) ^ first_dir) ? -1.0 : 1.0)*n*shift_step);
					if (!is_light_placement_valid(cand, room, pad))      continue;
					if (overlaps_other_room_obj(cand, check_coll_start)) continue; // intersects wall, pillar, etc.
					lights.push_back(cand);
					light_placed = 2;
					break;
				} // for n
				if (!allow_mult && light_placed) break;
			} // for d
		} // for D
	}
	if (light_placed) {
		cube_t &cur_light(lights.back());
		cube_t light_exp(cur_light);
		light_exp.expand_by_xy(get_doorway_width());
		
		// check doors if placed off-center or centered but close to the room bounds; only needed for non-centered lights, lights in small rooms, and backrooms
		if (light_placed == 2 || is_room_backrooms(room) || !room.contains_cube_xy(light_exp)) {
			cube_t test_cube(cur_light);
			test_cube.z1() -= 0.4*window_vspacing; // lower Z1 so that it's guaranteed to overlap a door

			// maybe should exclude basement doors, since they don't show as open? but then it would be wrong if I later draw basement doors;
			// note that this test is conservative for cylindrical house lights
			if (is_cube_close_to_doorway(test_cube, room, 0.0, 1, 1)) { // inc_open=1, check_open_dir=1
				float const orig_z1(cur_light.z1());
				cur_light.z1() += 0.98*cur_light.dz(); // if light intersects door, move it up into the ceiling rather than letting it hang down into the room
				if (cur_light.z1() == cur_light.z2()) {cur_light.z1() = orig_z1;} // fix to avoid zero area cube assert due to FP error
			}
		}
	}
	// else place light on a wall instead? do we ever get here?
}

void building_t::try_place_light_on_wall(cube_t const &light, room_t const &room, bool room_dim, float zval, vect_cube_t &lights, rand_gen_t &rgen) const {
	float const wall_thickness(get_wall_thickness()), window_vspacing(get_window_vspace());
	float const length(light.dz()), radius(0.25*min(light.dx(), light.dy())), min_wall_spacing(2.0*radius);
	cube_t const room_bounds(get_walkable_room_bounds(room));
	if (min(room_bounds.dx(), room_bounds.dy()) < 3.0*min_wall_spacing) return; // room is too small; shouldn't happen
	bool const pref_dim(!room_dim); // shorter dim
	vect_door_stack_t const &doorways(get_doorways_for_room(room, zval)); // must use floor zval
	cube_t c;
	c.z2() = light.z2() - 0.1*window_vspacing;
	c.z1() = c.z2() - 2.0*radius;

	for (unsigned n = 0; n < 100; ++n) { // 100 tries
		bool const dim((n < 10) ? pref_dim : rgen.rand_bool()), dir(rgen.rand_bool());
		float const wall_edge_spacing(max(min_wall_spacing, 0.25f*room_bounds.get_sz_dim(!dim))); // not near a corner of the room
		float const wall_pos(rgen.rand_uniform((room_bounds.d[!dim][0] + wall_edge_spacing), (room_bounds.d[!dim][1] - wall_edge_spacing)));
		float const wall_face(room_bounds.d[dim][dir]);
		c.d[dim][ dir] = wall_face; // flush with wall
		c.d[dim][!dir] = wall_face + (dir ? -1.0 : 1.0)*length; // expand outward
		set_wall_width(c, wall_pos, radius, !dim);
		cube_t c_exp(c);
		c_exp.expand_by_xy(0.5*wall_thickness);
		c_exp.d[dim][!dir] += (dir ? -1.0 : 1.0)*2.0*(length + radius); // add some clearance in front
		if (interior->is_blocked_by_stairs_or_elevator(c_exp)) continue; // check stairs and elevators; no other room objects have been placed yet
		cube_t door_test_cube(c_exp);
		door_test_cube.expand_in_dim(!dim, 1.0*radius); // not too close to doors
		bool bad_place(0);

		for (auto const &d : doorways) {
			if (d.get_open_door_bcube_for_room(room).intersects(door_test_cube)) {bad_place = 1; break;}
		}
		if (bad_place) continue;
		if (!check_if_placed_on_interior_wall(c, room, dim, dir)) continue; // ensure the light is on a wall; is this really needed?
		if (!check_cube_within_part_sides(c)) continue; // handle non-cube buildings
		lights.push_back(c);
		break; // success/done
	} // for n
}

