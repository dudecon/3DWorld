// 3D World - City Generation
// by Frank Gennari
// 2/10/18

#include "3DWorld.h"
#include "mesh.h"
#include "heightmap.h"
#include "file_utils.h"
#include "draw_utils.h"
#include "shaders.h"

using std::string;

bool const CHECK_HEIGHT_BORDER_ONLY = 1; // choose building site to minimize edge discontinuity rather than amount of land that needs to be modified
float const ROAD_HEIGHT = 0.001;


extern int rand_gen_index, display_mode;
extern float water_plane_z, shadow_map_pcf_offset, cobj_z_bias;


struct city_params_t {

	unsigned num_cities, num_samples, city_size, city_border, slope_width;
	float road_width, road_spacing;

	city_params_t() : num_cities(0), num_samples(100), city_size(0), city_border(0), slope_width(0), road_width(0.0), road_spacing(0.0) {}
	bool enabled() const {return (num_cities > 0 && city_size > 0);}
	bool roads_enabled() const {return (road_width > 0.0 && road_spacing > 0.0);}
	float get_road_ar() const {return nearbyint(road_spacing/road_width);} // round to nearest texture multiple
	static bool read_error(string const &str) {cout << "Error reading city config option " << str << "." << endl; return 0;}

	bool read_option(FILE *fp) {
		char strc[MAX_CHARS] = {0};
		if (!read_str(fp, strc)) return 0;
		string const str(strc);

		if (str == "num_cities") {
			if (!read_uint(fp, num_cities)) {return read_error(str);}
		}
		else if (str == "num_samples") {
			if (!read_uint(fp, num_samples) || num_samples == 0) {return read_error(str);}
		}
		else if (str == "city_size") {
			if (!read_uint(fp, city_size)) {return read_error(str);}
		}
		else if (str == "city_border") {
			if (!read_uint(fp, city_border)) {return read_error(str);}
		}
		else if (str == "slope_width") {
			if (!read_uint(fp, slope_width)) {return read_error(str);}
		}
		else if (str == "road_width") {
			if (!read_float(fp, road_width) || road_width < 0.0) {return read_error(str);}
		}
		else if (str == "road_spacing") {
			if (!read_float(fp, road_spacing) || road_spacing < 0.0) {return read_error(str);}
		}
		else {
			cout << "Unrecognized city keyword in input file: " << str << endl;
			return 0;
		}
		return 1;
	}
}; // city_params_t

city_params_t city_params;


struct heightmap_query_t {
	float *heightmap;
	unsigned xsize, ysize;

	heightmap_query_t() : heightmap(nullptr), xsize(0), ysize(0) {}
	heightmap_query_t(float *hmap, unsigned xsize_, unsigned ysize_) : heightmap(hmap), xsize(xsize_), ysize(ysize_) {}
	float get_x_value(int x) const {return get_xval(x - int(xsize)/2);} // convert from center to LLC
	float get_y_value(int y) const {return get_yval(y - int(ysize)/2);}
	int get_x_pos(float x) const {return (get_xpos(x) + int(xsize)/2);}
	int get_y_pos(float y) const {return (get_ypos(y) + int(ysize)/2);}

	bool is_valid_region(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		return (x1 < x2 && y1 < y2 && x2 <= xsize && y2 <= ysize);
	}
	float any_underwater(unsigned x1, unsigned y1, unsigned x2, unsigned y2, bool check_border=0) const {
		assert(is_valid_region(x1, y1, x2, y2));

		for (unsigned y = y1; y < y2; ++y) {
			for (unsigned x = x1; x < x2; ++x) {
				if (check_border && y != y1 && y != y2-1 && x == x1+1) {x = x2-1;} // jump to right edge
				if (heightmap[y*xsize + x] < water_plane_z) return 1;
			}
		}
		return 0;
	}
};

class city_plot_gen_t : public heightmap_query_t {

protected:
	struct rect_t {
		unsigned x1, y1, x2, y2;
		rect_t() : x1(0), y1(0), x2(0), y2(0) {}
		rect_t(unsigned x1_, unsigned y1_, unsigned x2_, unsigned y2_) : x1(x1_), y1(y1_), x2(x2_), y2(y2_) {}
		bool is_valid() const {return (x1 < x2 && y1 < y2);}
		unsigned get_area() const {return (x2 - x1)*(y2 - y1);}
		bool operator== (rect_t const &r) const {return (x1 == r.x1 && y1 == r.y1 && x2 == r.x2 && y2 == r.y2);}
		bool has_overlap(rect_t const &r) const {return (x1 < r.x2 && y1 < r.y2 && r.x1 < x2 && r.y1 < y2);}
	};

	int last_rgi;
	rand_gen_t rgen;
	vector<rect_t> used;
	vector<cube_t> plots; // same size as used

	bool overlaps_used(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		rect_t const cur(x1, y1, x2, y2);
		for (vector<rect_t>::const_iterator i = used.begin(); i != used.end(); ++i) {if (i->has_overlap(cur)) return 1;} // simple linear iteration
		return 0;
	}
	cube_t add_plot(unsigned x1, unsigned y1, unsigned x2, unsigned y2, float elevation) {
		cube_t bcube;
		bcube.x1() = get_x_value(x1);
		bcube.x2() = get_x_value(x2);
		bcube.y1() = get_y_value(y1);
		bcube.y2() = get_y_value(y2);
		bcube.z1() = bcube.z2() = elevation;
		plots.push_back(bcube);
		used.emplace_back(x1, y1, x2, y2);
		return bcube;
	}
	float get_avg_height(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		assert(is_valid_region(x1, y1, x2, y2));
		float sum(0.0), denom(0.0);

		for (unsigned y = y1; y < y2; ++y) {
			for (unsigned x = x1; x < x2; ++x) {
				if (CHECK_HEIGHT_BORDER_ONLY && y != y1 && y != y2-1 && x == x1+1) {x = x2-1;} // jump to right edge
				sum   += heightmap[y*xsize + x];
				denom += 1.0;
			}
		}
		return sum/denom;
	}
	float get_rms_height_diff(unsigned x1, unsigned y1, unsigned x2, unsigned y2) const {
		float const avg(get_avg_height(x1, y1, x2, y2));
		float diff(0.0);

		for (unsigned y = y1; y < y2; ++y) {
			for (unsigned x = x1; x < x2; ++x) {
				if (CHECK_HEIGHT_BORDER_ONLY && y != y1 && y != y2-1 && x == x1+1) {x = x2-1;} // jump to right edge
				float const delta(heightmap[y*xsize + x] - avg);
				diff += delta*delta; // square the difference
			}
		}
		return diff;
	}
public:
	city_plot_gen_t() : last_rgi(0) {}

	void init(float *heightmap_, unsigned xsize_, unsigned ysize_) {
		heightmap = heightmap_; xsize = xsize_; ysize = ysize_;
		assert(heightmap != nullptr);
		assert(xsize > 0 && ysize > 0); // any size is okay
		if (rand_gen_index != last_rgi) {rgen.set_state(rand_gen_index, 12345); last_rgi = rand_gen_index;} // only when rand_gen_index changes
	}
	bool find_best_city_location(unsigned width, unsigned height, unsigned border, unsigned num_samples, unsigned &x_llc, unsigned &y_llc) {
		assert(num_samples > 0);
		assert((width + 2*border) < xsize && (height + 2*border) < ysize); // otherwise the city can't fit in the map
		unsigned const num_iters(100*num_samples); // upper bound
		unsigned xend(xsize - width - 2*border + 1), yend(ysize - width - 2*border + 1); // max rect LLC, inclusive
		unsigned num_cands(0);
		float best_diff(0.0);

		for (unsigned n = 0; n < num_iters; ++n) { // find min RMS height change across N samples
			unsigned const x1(border + (rgen.rand()%xend)), y1(border + (rgen.rand()%yend));
			unsigned const x2(x1 + width), y2(y1 + height);
			if (overlaps_used (x1, y1, x2, y2)) continue; // skip
			if (any_underwater(x1, y1, x2, y2, CHECK_HEIGHT_BORDER_ONLY)) continue; // skip
			float const diff(get_rms_height_diff(x1, y1, x2, y2));
			if (num_cands == 0 || diff < best_diff) {x_llc = x1; y_llc = y1; best_diff = diff;}
			if (++num_cands == num_samples) break; // done
		} // for n
		if (num_cands == 0) return 0;
		cout << "City cands: " << num_cands << ", diff: " << best_diff << ", loc: " << x_llc << "," << y_llc << endl;
		return 1; // success
	}
	float flatten_region(unsigned x1, unsigned y1, unsigned x2, unsigned y2, unsigned slope_width, float const *const height=nullptr) {
		assert(is_valid_region(x1, y1, x2, y2));
		float const delta_h = 0.0; // for debugging in map view
		float const elevation(height ? *height : (get_avg_height(x1, y1, x2, y2) + delta_h));

		for (unsigned y = max((int)y1-(int)slope_width, 0); y < min(y2+slope_width, ysize); ++y) {
			for (unsigned x = max((int)x1-(int)slope_width, 0); x < min(x2+slope_width, xsize); ++x) {
				float &h(heightmap[y*xsize + x]);
				
				if (slope_width > 0) {
					float const dx(max(0, max(((int)x1 - (int)x), ((int)x - (int)x2 + 1))));
					float const dy(max(0, max(((int)y1 - (int)y), ((int)y - (int)y2 + 1))));
					float mix(min(1.0f, sqrt(dx*dx + dy*dy)/slope_width));
					mix = mix * mix * (3.0 - 2.0 * mix); // cubic Hermite interoplation (smoothstep)
					h = mix*h + (1.0 - mix)*elevation;
				}
				else {h = elevation;}
			}
		}
		return elevation;
	}
	vector3d const get_query_xlate() const {
		return vector3d((world_mode == WMODE_INF_TERRAIN) ? vector3d((xoff - xoff2)*DX_VAL, (yoff - yoff2)*DY_VAL, 0.0) : zero_vector);
	}
	bool check_plot_sphere_coll(point const &pos, float radius, bool xy_only=1) const {
		if (plots.empty()) return 0;
		point const sc(pos - get_query_xlate());

		for (auto i = plots.begin(); i != plots.end(); ++i) {
			if (xy_only ? sphere_cube_intersect_xy(sc, radius, *i) : sphere_cube_intersect(sc, radius, *i)) return 1;
		}
		return 0;
	}
}; // city_plot_gen_t


enum {TID_SIDEWLAK=0, TID_STRAIGHT, TID_BEND_90, TID_3WAY,   TID_4WAY,   NUM_RD_TIDS };
enum {TYPE_PLOT   =0, TYPE_RSEG,    TYPE_ISEC2,  TYPE_ISEC3, TYPE_ISEC4, NUM_RD_TYPES};

colorRGBA const road_color(WHITE); // all road parts are the same color, to make the textures match

class road_mat_mrg_t {

	bool inited;
	unsigned tids[NUM_RD_TIDS];

public:
	road_mat_mrg_t() : inited(0) {}

	void ensure_road_textures() {
		if (inited) return;
		timer_t timer("Load Road Textures");
		string const img_names[NUM_RD_TIDS] = {"sidewalk.jpg", "straight_road.jpg", "bend_90.jpg", "int_3_way.jpg", "int_4_way.jpg"};
		float const aniso[NUM_RD_TIDS] = {4.0, 16.0, 8.0, 8.0, 8.0};
		for (unsigned i = 0; i < NUM_RD_TIDS; ++i) {tids[i] = get_texture_by_name(("roads/" + img_names[i]), 0, 0, 1, aniso[i]);}
		inited = 1;
	}
	void set_texture(unsigned type) {
		assert(type < NUM_RD_TYPES);
		ensure_road_textures();
		select_texture(tids[type]);
	}
};

road_mat_mrg_t road_mat_mrg;


class city_road_gen_t {

	struct range_pair_t {
		unsigned s, e; // Note: e is one past the end
		range_pair_t() : s(0), e(0) {}
		void update(unsigned v) {
			if (s == 0 && e == 0) {s = v;} // first insert
			else {assert(s < e && v >= e);} // v must strictly increase
			e = v+1; // one past the end
		}
	};

	template<typename T> static void add_flat_road_quad(T const &r, quad_batch_draw &qbd, float ar) { // z1 == z2
		float const z(r.z1()); // z1
		point const pts[4] = {point(r.x1(), r.y1(), z), point(r.x2(), r.y1(), z), point(r.x2(), r.y2(), z), point(r.x1(), r.y2(), z)};
		qbd.add_quad_pts(pts, road_color, plus_z, r.get_tex_range(ar));
	}

	struct road_t : public cube_t {
		bool dim; // dim the road runs in
		bool slope; // 0: z1 applies to first (lower) point; 1: z1 applies to second (upper) point

		road_t() : dim(0), slope(0) {}
		road_t(cube_t const &c, bool dim_, bool slope_=0) : cube_t(c), dim(dim_), slope(slope_) {}
		road_t(point const &s, point const &e, float width, bool dim_, bool slope_=0) : dim(dim_), slope(slope_) {
			assert(s != e);
			assert(width > 0.0);
			vector3d const dw(0.5*width*cross_product((e - s), plus_z).get_norm());
			point const pts[4] = {(s - dw), (s + dw), (e + dw), (e - dw)};
			set_from_points(pts, 4);
		}
		float get_length() const {return (d[dim][1] - d[dim][0]);}
		float get_height() const {return (d[2  ][1] - d[2  ][0]);}
	};
	struct road_seg_t : public road_t {
		road_seg_t(cube_t const &c, bool dim_, bool slope_=0) : road_t(c, dim_, slope_) {}
		tex_range_t get_tex_range(float ar) const {return tex_range_t(0.0, 0.0, -ar, (dim ? -1.0 : 1.0), 0, dim);}

		void add_road_quad(quad_batch_draw &qbd, float ar) const { // specialized here for sloped roads
			if (d[2][0] == d[2][1]) {add_flat_road_quad(*this, qbd, ar); return;}
			bool const s(slope ^ dim);
			point pts[4] = {point(x1(), y1(), d[2][!s]), point(x2(), y1(), d[2][!s]), point(x2(), y2(), d[2][ s]), point(x1(), y2(), d[2][ s])};
			if (!dim) {swap(pts[0].z, pts[2].z);}
			vector3d const normal(cross_product((pts[2] - pts[1]), (pts[0] - pts[1])).get_norm());
			qbd.add_quad_pts(pts, road_color, normal, get_tex_range(ar));
		}
	};
	struct road_isec_t : public cube_t {
		uint8_t conn; // connected roads in {-x, +x, -y, +y}
		road_isec_t() : conn(15) {}
		road_isec_t(cube_t const &c, uint8_t conn_=15) : cube_t(c), conn(conn_) {}

		tex_range_t get_tex_range(float ar) const {
			switch (conn) {
			case 5 : return tex_range_t(0.0, 0.0, -1.0,  1.0, 0, 0); // 2-way: MX
			case 6 : return tex_range_t(0.0, 0.0,  1.0,  1.0, 0, 0); // 2-way: R0
			case 9 : return tex_range_t(0.0, 0.0, -1.0, -1.0, 0, 0); // 2-way: MXMY
			case 10: return tex_range_t(0.0, 0.0,  1.0, -1.0, 0, 0); // 2-way: MY
			case 7 : return tex_range_t(0.0, 0.0,  1.0,  1.0, 0, 0); // 3-way: R0
			case 11: return tex_range_t(0.0, 0.0, -1.0, -1.0, 0, 0); // 3-way: MY
			case 13: return tex_range_t(0.0, 0.0,  1.0, -1.0, 0, 1); // 3-way: R90MY
			case 14: return tex_range_t(0.0, 0.0, -1.0,  1.0, 0, 1); // 3-way: R90MX
			case 15: return tex_range_t(0.0, 0.0,  1.0,  1.0, 0, 0); // 4-way: R0
			default: assert(0);
			}
			return tex_range_t(0.0, 0.0, 1.0, 1.0); // never gets here
		}
	};
	struct road_plot_t : public cube_t {
		road_plot_t() {}
		road_plot_t(cube_t const &c) : cube_t(c) {}
		tex_range_t get_tex_range(float ar) const {return tex_range_t(0.0, 0.0, ar, ar);}
	};

	struct draw_state_t {
		shader_t s;
		vector3d xlate;
		bool use_smap, use_bmap;
	private:
		quad_batch_draw qbd_batched[NUM_RD_TYPES];
		bool emit_now;
		float ar;

	public:
		draw_state_t() : xlate(zero_vector), use_smap(0), use_bmap(0), emit_now(0), ar(1.0) {}
		void begin_tile(point const &pos) {emit_now = (use_smap && try_bind_tile_smap_at_point((pos + xlate), s));}

		void pre_draw() {
			if (use_smap) {
				setup_smoke_shaders(s, 0.0, 0, 0, 0, 1, 0, 0, 0, 1, use_bmap, 0, 0, 0, 0.0, 0.0, 0, 0, 1); // is_outside=1
				s.add_uniform_float("z_bias", cobj_z_bias);
				s.add_uniform_float("pcf_offset", 10.0*shadow_map_pcf_offset);
			}
			ar = city_params.get_road_ar();
		}
		void post_draw() {
			emit_now = 0;
			if (use_smap) {s.end_shader();}
			setup_smoke_shaders(s, 0.0, 0, 0, 0, 1, 0, 0, 0, 0, use_bmap, 0, 0, 0, 0.0, 0.0, 0, 0, 1); // is_outside=1
			
			for (unsigned i = 0; i < NUM_RD_TYPES; ++i) { // only unshadowed blocks
				road_mat_mrg.set_texture(i);
				qbd_batched[i].draw_and_clear();
			}
			s.end_shader();
		}
		template<typename T> void add_road_quad(T const &r, quad_batch_draw &qbd) const {add_flat_road_quad(r, qbd, ar);} // generic flat road case
		template<> void add_road_quad(road_seg_t  const &r, quad_batch_draw &qbd) const {r.add_road_quad(qbd, ar);}

		template<typename T> void draw_road_region(vector<T> const &v, range_pair_t const &rp, quad_batch_draw &cache, unsigned type_ix) {
			assert(rp.s <= rp.e && rp.e <= v.size());
			assert(type_ix < NUM_RD_TYPES);
			
			if (cache.empty()) { // generate and cache quads
				for (unsigned i = rp.s; i < rp.e; ++i) {add_road_quad(v[i], cache);}
			}
			if (emit_now) { // draw shadow blocks directly
				road_mat_mrg.set_texture(type_ix);
				cache.draw();
			}
			else {qbd_batched[type_ix].add_quads(cache);} // add non-shadow blocks for drawing later
		}
	};

	class road_network_t {
		vector<road_t> roads; // full overlapping roads, for collisions, etc.
		vector<road_seg_t> segs; // non-overlapping road segments, for drawing with textures
		vector<road_isec_t> isecs[3]; // for drawing with textures: {4-way, 3-way, 2-way}
		vector<road_plot_t> plots; // plots of land that can hold buildings
		cube_t bcube;
		//string city_name; // future work

		static uint64_t get_tile_id_for_cube(cube_t const &c) {return get_tile_id_containing_point_no_xyoff(c.get_cube_center());}

		struct cmp_by_tile { // not the most efficient solution, but no memory overhead
			bool operator()(cube_t const &a, cube_t const &b) const {return (get_tile_id_for_cube(a) < get_tile_id_for_cube(b));}
		};
		struct tile_block_t { // collection of road parts for a given tile
			range_pair_t ranges[NUM_RD_TYPES]; // {xsegs, ysegs, isecs, plots}
			quad_batch_draw quads[NUM_RD_TYPES];
			cube_t bcube;
			tile_block_t(cube_t const &bcube_) : bcube(bcube_) {}
		};
		vector<tile_block_t> tile_blocks;

		template<typename T> void add_tile_blocks(vector<T> &v, map<uint64_t, unsigned> &tile_to_block_map, unsigned type_ix) {
			assert(type_ix < NUM_RD_TYPES);
			sort(v.begin(), v.end(), cmp_by_tile());

			for (unsigned i = 0; i < v.size(); ++i) {
				uint64_t const tile_id(get_tile_id_for_cube(v[i]));
				auto it(tile_to_block_map.find(tile_id));
				unsigned block_id(0);
			
				if (it == tile_to_block_map.end()) { // not found, add new block
					tile_to_block_map[tile_id] = block_id = tile_blocks.size();
					tile_blocks.push_back(tile_block_t(v[i]));
				}
				else {block_id = it->second;}
				assert(block_id < tile_blocks.size());
				tile_blocks[block_id].ranges[type_ix].update(i);
				tile_blocks[block_id].bcube.union_with_cube(v[i]);
			} // for i
		}

	public:
		road_network_t() : bcube(all_zeros) {}
		road_network_t(cube_t const &bcube_) : bcube(bcube_) {bcube.d[2][1] += ROAD_HEIGHT;} // make it nonzero size
		cube_t const &get_bcube() const {return bcube;}
		void set_bcube(cube_t const &bcube_) {bcube = bcube_;}
		unsigned num_roads() const {return roads.size();}
		bool empty() const {return roads.empty();}

		void clear() {
			roads.clear();
			segs.clear();
			plots.clear();
			for (unsigned i = 0; i < 3; ++i) {isecs[i].clear();}
			tile_blocks.clear();
		}
		bool gen_road_grid(float road_width, float road_spacing) {
			cube_t const &region(bcube); // use our bcube as the region to process
			vector3d const size(region.get_size());
			assert(size.x > 0.0 && size.y > 0.0);
			float const half_width(0.5*road_width), road_pitch(road_width + road_spacing);
			float const zval(region.d[2][0] + ROAD_HEIGHT);

			// create a grid, for now; crossing roads will overlap
			for (float x = region.x1()+half_width; x < region.x2()-half_width; x += road_pitch) { // shrink to include centerlines
				roads.emplace_back(point(x, region.y1(), zval), point(x, region.y2(), zval), road_width, false);
			}
			unsigned const num_x(roads.size());

			for (float y = region.y1()+half_width; y < region.y2()-half_width; y += road_pitch) { // shrink to include centerlines
				roads.emplace_back(point(region.x1(), y, zval), point(region.x2(), y, zval), road_width, true);
			}
			unsigned const num_r(roads.size()), num_y(num_r - num_x);
			if (num_x <= 1 || num_y <= 1) {clear(); return 0;} // not enough space for roads
			bcube.x1() = roads[0      ].x1(); // actual bcube x1 from first x road
			bcube.x2() = roads[num_x-1].x2(); // actual bcube x2 from last  x road
			bcube.y1() = roads[num_x  ].y1(); // actual bcube y1 from first y road
			bcube.y2() = roads[num_r-1].y2(); // actual bcube y2 from last  y road

			// create road segments and intersections
			segs .reserve(num_x*(num_y-1) + (num_x-1)*num_y + 4); // X + Y segments, allocate one extra per side for connectors
			plots.reserve((num_x-1)*(num_y-1));

			if (num_x > 2 && num_y > 2) {
				isecs[0].reserve(4); // 2-way, always exactly 4 at each corner
				isecs[1].reserve(2*((num_x-2) + (num_y-2)) + 4); // 3-way, allocate one extra per side for connectors
				isecs[2].reserve((num_x-2)*(num_y-2)); // 4-way
			}
			for (unsigned x = 0; x < num_x; ++x) {
				for (unsigned y = num_x; y < num_r; ++y) {
					bool const FX(x == 0), FY(y == num_x), LX(x+1 == num_x), LY(y+1 == num_r);
					cube_t const &rx(roads[x]), &ry(roads[y]);
					unsigned const num_conn((!FX) + (!LX) + (!FY) + (!LY));
					if (num_conn < 2) continue; // error?
					uint8_t const conn(((!FX) << 0) | ((!LX) << 1) | ((!FY) << 2) | ((!LY) << 3)); // 1-15
					isecs[num_conn - 2].emplace_back(cube_t(rx.x1(), rx.x2(), ry.y1(), ry.y2(), zval, zval), conn); // intersections
					
					if (!LX) { // skip last y segment
						cube_t const &rxn(roads[x+1]);
						segs.emplace_back(cube_t(rx.x2(), rxn.x1(), ry.y1(), ry.y2(), zval, zval), false); // y-segments
					}
					if (!LY) { // skip last x segment
						cube_t const &ryn(roads[y+1]);
						segs.emplace_back(cube_t(rx.x1(), rx.x2(), ry.y2(), ryn.y1(), zval, zval), true); // x-segments

						if (!LX) { // skip last y segment
							cube_t const &rxn(roads[x+1]);
							plots.push_back(cube_t(rx.x2(), rxn.x1(), ry.y2(), ryn.y1(), zval, zval)); // plots between roads
						}
					}
				} // for y
			} // for x
			return 1;
		}
	private:
		int find_conn_int_seg(cube_t const &c, bool dim, bool dir) const {
			for (unsigned i = 0; i < segs.size(); ++i) {
				road_seg_t const &s(segs[i]);
				if (s.dim == dim) continue; // not perp dim
				if (s.d[dim][dir] != bcube.d[dim][dir]) continue; // not on edge of road grid
				if (s.d[!dim][1] < c.d[!dim][0] || s.d[!dim][0] > c.d[!dim][1]) continue; // no overlap/projection in other dim
				if (c.d[!dim][0] > s.d[!dim][0] && c.d[!dim][1] < s.d[!dim][1]) return i; // c contained in segment in other dim, this is the one we want
				return -1; // partial overlap in other dim, can't split, fail
			} // for i
			return -1; // not found
		}
	public:
		bool check_valid_conn_intersection(cube_t const &c, bool dim, bool dir) const {return (find_conn_int_seg(c, dim, dir) >= 0);}
		void insert_conn_intersection(cube_t const &c, bool dim, bool dir) {
			int const seg_id(find_conn_int_seg(c, dim, dir));
			assert(seg_id >= 0 && (unsigned)seg_id < segs.size());
			segs.push_back(segs[seg_id]); // clone the segment first
			segs[seg_id].d[!dim][1] = c.d[!dim][0]; // low part
			segs.back() .d[!dim][0] = c.d[!dim][1]; // high part
			cube_t ibc(segs[seg_id]); // intersection bcube
			ibc.d[!dim][0] = c.d[!dim][0]; // copy width from c
			ibc.d[!dim][1] = c.d[!dim][1];
			uint8_t const conns[4] = {7, 11, 13, 14};
			isecs[1].emplace_back(ibc, conns[2*(!dim) + dir]);
		}
		bool create_connector_road(cube_t const &bcube1, cube_t const &bcube2, road_network_t &rn1, road_network_t &rn2, heightmap_query_t &hq, float road_width, float conn_pos, bool dim) {
			bool const dir(bcube1.d[dim][0] < bcube2.d[dim][0]);
			point p1, p2;
			p1.z   = bcube1.d[2][1];
			p2.z   = bcube2.d[2][1];
			p1[!dim] = p2[!dim] = conn_pos;
			p1[ dim] = bcube1.d[dim][ dir];
			p2[ dim] = bcube2.d[dim][!dir];
			bool const slope((p1.z < p2.z) ^ dir);
			road_t road(p1, p2, road_width, dim, slope);
			if (!rn1.check_valid_conn_intersection(road, dim, dir) || !rn2.check_valid_conn_intersection(road, dim, !dir)) return 0; // invalid, don't make any changes
			int const x1(hq.get_x_pos(road.x1())), y1(hq.get_y_pos(road.y1())), x2(hq.get_x_pos(road.x2())), y2(hq.get_y_pos(road.y2()));
			if (hq.any_underwater(x1, y1, x2, y2)) return 0; // underwater
			rn1.insert_conn_intersection(road, dim,  dir);
			rn2.insert_conn_intersection(road, dim, !dir);
			// FIXME: use hq to modify terrain height around road, or make road follow terrain countour (could do this in split_connector_roads())
			roads.push_back(road);
			return 1; // success
		}
		void split_connector_roads(float road_spacing) {
			// Note: here we use segs, maybe isecs, but not plots
			for (auto r = roads.begin(); r != roads.end(); ++r) {
				bool const d(r->dim), slope(r->slope);
				float const len(r->get_length()), z1(r->d[2][slope]), z2(r->d[2][!slope]);
				assert(len > 0.0);
				unsigned const num_segs(ceil(len/road_spacing));
				cube_t c(*r); // start by copying the road's bcube
				
				for (unsigned n = 0; n < num_segs; ++n) {
					c.d[d][1] = min(r->d[d][1], (c.d[d][0] + road_spacing)); // clamp to original road end
					for (unsigned e = 0; e < 2; ++e) {c.d[2][e] = z1 + (z2 - z1)*((c.d[d][e] - r->d[d][0])/len);} // interpolate road height across segments
					if (c.d[2][1] < c.d[2][0]) swap(c.d[2][0], c.d[2][1]); // swap zvals if needed
					assert(c.is_normalized());
					segs.emplace_back(c, d, r->slope);
					c.d[d][0] = c.d[d][1]; // shift segment end point
				} // for n
			} // for r
		}
		void gen_tile_blocks() {
			tile_blocks.clear(); // should already be empty?
			map<uint64_t, unsigned> tile_to_block_map;
			add_tile_blocks(segs,  tile_to_block_map, TYPE_RSEG);
			add_tile_blocks(plots, tile_to_block_map, TYPE_PLOT);
			for (unsigned i = 0; i < 3; ++i) {add_tile_blocks(isecs[i], tile_to_block_map, (TYPE_ISEC2 + i));}
			//cout << "tile_to_block_map: " << tile_to_block_map.size() << ", tile_blocks: " << tile_blocks.size() << endl;
		}
		void get_road_bcubes(vector<cube_t> &bcubes) const {
			for (auto r = roads.begin(); r != roads.end(); ++r) {bcubes.push_back(*r);}
		}
		void get_plot_bcubes(vector<cube_t> &bcubes) const {
			for (auto r = plots.begin(); r != plots.end(); ++r) {bcubes.push_back(*r);}
		}
		void draw(draw_state_t &dstate) {
			if (empty()) return;
			cube_t const bcube_x(bcube + dstate.xlate);
			if (!camera_pdu.cube_visible(bcube_x)) return; // VFC
			if (!dist_less_than(camera_pdu.pos, bcube_x.closest_pt(camera_pdu.pos), get_draw_tile_dist())) return; // too far

			for (auto b = tile_blocks.begin(); b != tile_blocks.end(); ++b) {
				if (!camera_pdu.cube_visible(b->bcube + dstate.xlate)) continue; // VFC
				dstate.begin_tile(b->bcube.get_cube_center());
				dstate.draw_road_region(segs,  b->ranges[TYPE_RSEG], b->quads[TYPE_RSEG], TYPE_RSEG); // road segments
				dstate.draw_road_region(plots, b->ranges[TYPE_PLOT], b->quads[TYPE_PLOT], TYPE_PLOT); // plots
				for (unsigned i = 0; i < 3; ++i) {dstate.draw_road_region(isecs[i], b->ranges[TYPE_ISEC2 + i], b->quads[TYPE_ISEC2 + i], (TYPE_ISEC2 + i));} // intersections
			} // for b
		}
	}; // road_network_t

	vector<road_network_t> road_networks; // one per city
	road_network_t global_rn; // connects cities together; no plots
	draw_state_t dstate;

public:
	void gen_roads(cube_t const &region, float road_width, float road_spacing) {
		timer_t timer("Gen Roads");
		road_networks.push_back(road_network_t(region));
		if (!road_networks.back().gen_road_grid(road_width, road_spacing)) {road_networks.pop_back();}
		else {cout << "Roads: " << road_networks.back().num_roads() << endl;}
	}
	bool connect_two_cities(unsigned city1, unsigned city2, heightmap_query_t &hq, float road_width) {
		assert(city1 < road_networks.size() && city2 < road_networks.size());
		assert(city1 != city2); // check for self reference
		cout << "Connect city " << city1 << " and " << city2 << endl;
		road_network_t &rn1(road_networks[city1]), &rn2(road_networks[city2]);
		cube_t const &bcube1(rn1.get_bcube()), &bcube2(rn2.get_bcube());
		assert(!bcube1.intersects_xy(bcube2));
		rand_gen_t rgen;
		rgen.set_state(city1+111, city2+222);
		// Note: cost function should include road length, number of jogs, total elevation change, and max slope

		for (unsigned d = 0; d < 2; ++d) {
			float const shared_min(max(bcube1.d[d][0], bcube2.d[d][0])), shared_max(min(bcube1.d[d][1], bcube2.d[d][1]));
			
			if (shared_max - shared_min > road_width) { // can connect with single road segment in dim !d, if the terrain in between is passable
				cout << "Shared dim " << d << endl;
				float const val1(shared_min+0.5*road_width), val2(shared_max-0.5*road_width);
				float conn_pos(0.5*(val1 + val2)); // center of connecting segment: start by using center of city overlap area

				for (unsigned n = 0; n < 10; ++n) { // make up to 10 attempts at connecting the cities with a straight line
					if (global_rn.create_connector_road(bcube1, bcube2, rn1, rn2, hq, road_width, conn_pos, !d)) return 1; // done
					conn_pos = (val1 + (val2 - val1)*rgen.rand_float()); // chose a random new connection point and try it
				}
			}
		} // for d
		// WRITE: connect with multiple road segments using jogs
		return 0;
	}
	void connect_all_cities(float *heightmap, unsigned xsize, unsigned ysize, float road_width, float road_spacing) {
		if (road_width == 0.0 || road_spacing == 0.0) return; // no roads
		unsigned const num_cities(road_networks.size());
		if (num_cities < 2) return; // not cities to connect
		timer_t timer("Connect Cities");
		heightmap_query_t hq(heightmap, xsize, ysize);
		vector<unsigned> is_conn(num_cities, 0); // start with all cities unconnected
		vector<unsigned> connected; // cities that are currently connected
		unsigned cur_city(0);
		cube_t all_bcube(road_networks.front().get_bcube());
		for (auto i = road_networks.begin()+1; i != road_networks.end(); ++i) {all_bcube.union_with_cube(i->get_bcube());}
		global_rn.set_bcube(all_bcube); // unioned across all cities (FIXME: implies that roads can't go outside of the union bcube?)

		// Note: may want to spatially sort cities to avoid bad connection order
		while (connected.size() < num_cities) {
			for (; is_conn[cur_city]; ++cur_city) {assert(cur_city < num_cities);} // find next unconnected city
			assert(cur_city < num_cities);
			point const center(road_networks[cur_city].get_bcube().get_cube_center());
			float dmin_sq(0.0);
			unsigned closest_conn(0);
			cout << "Select city " << cur_city << ", connected " << connected.size() << " of " << num_cities << endl;
			
			if (connected.empty()) { // first city, find closest other city
				for (unsigned i = 1; i < num_cities; ++i) {
					float const dist_sq(p2p_dist_sq(center, road_networks[i].get_bcube().get_cube_center()));
					if (dmin_sq == 0.0 || dist_sq < dmin_sq) {closest_conn = i; dmin_sq = dist_sq;}
				}
			}
			else { // find closest connected city
				for (auto i = connected.begin(); i != connected.end(); ++i) {
					float const dist_sq(p2p_dist_sq(center, road_networks[*i].get_bcube().get_cube_center()));
					if (dmin_sq == 0.0 || dist_sq < dmin_sq) {closest_conn = *i; dmin_sq = dist_sq;}
				}
			}
			cout << "Closest is " << closest_conn << ", dist " << sqrt(dmin_sq) << endl;
			
			if (!connect_two_cities(cur_city, closest_conn, hq, road_width)) {
				cout << "Unable to connect cities " << cur_city << " and " << closest_conn << endl; // FIXME: still gets marked as connected
			}
			is_conn[cur_city] = 1;
			connected.push_back(cur_city);
			if (!is_conn[closest_conn]) {is_conn[closest_conn] = 1;	connected.push_back(closest_conn);} // first city is also now connected
		} // end while()
		global_rn.split_connector_roads(road_spacing);
	}
	void gen_tile_blocks() {
		for (auto i = road_networks.begin(); i != road_networks.end(); ++i) {i->gen_tile_blocks();}
		global_rn.gen_tile_blocks();
	}
	void get_all_road_bcubes(vector<cube_t> &bcubes) const {
		global_rn.get_road_bcubes(bcubes); // not sure if this should be included
		for (auto r = road_networks.begin(); r != road_networks.end(); ++r) {r->get_road_bcubes(bcubes);}
	}
	void get_all_plot_bcubes(vector<cube_t> &bcubes) const { // Note: no global_rn
		for (auto r = road_networks.begin(); r != road_networks.end(); ++r) {r->get_plot_bcubes(bcubes);}
	}
	void draw(vector3d const &xlate) { // non-const because qbd is modified
		if (road_networks.empty() && global_rn.empty()) return;
		//timer_t timer("Draw Roads");
		fgPushMatrix();
		translate_to(xlate);
		glDepthFunc(GL_LEQUAL); // helps prevent Z-fighting
		dstate.use_smap = shadow_map_enabled();
		dstate.xlate    = xlate;
		dstate.pre_draw();
		for (auto r = road_networks.begin(); r != road_networks.end(); ++r) {r->draw(dstate);}
		global_rn.draw(dstate);
		dstate.post_draw();
		glDepthFunc(GL_LESS);
		fgPopMatrix();
	}
}; // city_road_gen_t


class city_gen_t : public city_plot_gen_t {

	city_road_gen_t road_gen;

public:
	bool gen_city(city_params_t const &params, cube_t &cities_bcube) {
		timer_t t("Choose City Location");
		unsigned x1(0), y1(0);
		if (!find_best_city_location(params.city_size, params.city_size, params.city_border, params.num_samples, x1, y1)) return 0;
		unsigned const x2(x1 + params.city_size), y2(y1 + params.city_size);
		float const elevation(flatten_region(x1, y1, x2, y2, params.slope_width));
		cube_t const pos_range(add_plot(x1, y1, x2, y2, elevation));
		if (cities_bcube.is_all_zeros()) {cities_bcube = pos_range;} else {cities_bcube.union_with_cube(pos_range);}
		if (params.roads_enabled()) {road_gen.gen_roads(pos_range, params.road_width, params.road_spacing);}
		return 1;
	}
	void gen_cities(city_params_t const &params) {
		if (params.num_cities == 0) return;
		cube_t cities_bcube(all_zeros);
		for (unsigned n = 0; n < params.num_cities; ++n) {gen_city(params, cities_bcube);}
		bool const is_const_zval(cities_bcube.d[2][0] == cities_bcube.d[2][1]);
		if (!cities_bcube.is_all_zeros()) {set_buildings_pos_range(cities_bcube, is_const_zval);}
		road_gen.connect_all_cities(heightmap, xsize, ysize, params.road_width, params.road_spacing);
		road_gen.gen_tile_blocks();
	}
	void get_all_road_bcubes(vector<cube_t> &bcubes) const {road_gen.get_all_road_bcubes(bcubes);}
	void get_all_plot_bcubes(vector<cube_t> &bcubes) const {road_gen.get_all_plot_bcubes(bcubes);}

	void draw(bool shadow_only, int reflection_pass, vector3d const &xlate) { // for now, there are only roads
		if (!shadow_only && reflection_pass == 0) {road_gen.draw(xlate);} // roads don't cast shadows and aren't reflected in water
		// buildings are drawn through draw_buildings()
	}
}; // city_gen_t

city_gen_t city_gen;


bool parse_city_option(FILE *fp) {return city_params.read_option(fp);}
bool have_cities() {return city_params.enabled();}
float get_road_max_len() {return city_params.road_spacing;}

void gen_cities(float *heightmap, unsigned xsize, unsigned ysize) {
	if (!have_cities()) return; // nothing to do
	city_gen.init(heightmap, xsize, ysize); // only need to call once for any given heightmap
	city_gen.gen_cities(city_params);
}
void get_city_road_bcubes(vector<cube_t> &bcubes) {city_gen.get_all_road_bcubes(bcubes);}
void get_city_plot_bcubes(vector<cube_t> &bcubes) {city_gen.get_all_plot_bcubes(bcubes);}
void draw_cities(bool shadow_only, int reflection_pass, vector3d const &xlate) {city_gen.draw(shadow_only, reflection_pass, xlate);}

bool check_city_sphere_coll(point const &pos, float radius) {
	if (!have_cities()) return 0;
	point center(pos);
	if (world_mode == WMODE_INF_TERRAIN) {center += vector3d(xoff*DX_VAL, yoff*DY_VAL, 0.0);} // apply xlate for all static objects
	return city_gen.check_plot_sphere_coll(center, radius);
}
bool check_valid_scenery_pos(point const &pos, float radius) {
	if (check_buildings_sphere_coll(pos, radius, 1, 1)) return 0; // apply_tt_xlate=1, xy_only=1
	if (check_city_sphere_coll(pos, radius)) return 0;
	return 1;
}

