// 3D World - Voxel Header
// by Frank Gennari
// 2/25/12
#ifndef _VOXELS_H_
#define _VOXELS_H_

#include "3DWorld.h"
#include "model3d.h"

struct coll_tquad;


struct voxel_params_t {

	// generation parameters
	float isolevel, elasticity, mag, freq, atten_thresh, tex_scale, noise_scale, noise_freq, tex_mix_saturate;
	float ao_radius, ao_weight_scale, ao_atten_power;
	bool make_closed_surface, invert, remove_unconnected, remove_under_mesh;
	unsigned atten_at_edges; // 0=no atten, 1=top only, 2=all 5 edges (excludes the bottom);
	unsigned keep_at_scene_edge; // 0=don't keep, 1=always keep, 2=only when scrolling
	int geom_rseed;
	
	// rendering parameters
	int texture_rseed;
	unsigned tids[2];
	colorRGBA colors[2];
	colorRGBA base_color;

	voxel_params_t() : isolevel(0.0), elasticity(0.5), mag(1.0), freq(1.0), atten_thresh(1.0), tex_scale(1.0), noise_scale(0.1),
		noise_freq(1.0), tex_mix_saturate(5.0), ao_radius(1.0), ao_weight_scale(2.0), ao_atten_power(1.0), make_closed_surface(0),
		invert(0), remove_unconnected(0), remove_under_mesh(0), atten_at_edges(0), keep_at_scene_edge(0), geom_rseed(123), texture_rseed(321)
	{
			tids[0] = tids[1] = 0; colors[0] = colors[1] = base_color = WHITE;
	}
};


// stored internally in yxz order
template<typename V> class voxel_grid : public vector<V> {
public:
	unsigned nx, ny, nz, xblocks, yblocks;
	vector3d vsz; // size of a voxel in x,y,z
	point center, lo_pos;

	voxel_grid() : nx(0), ny(0), nz(0), xblocks(0), yblocks(0), vsz(zero_vector) {}
	void init(unsigned nx_, unsigned ny_, unsigned nz_, vector3d const &vsz_, point const &center_, V default_val);
	bool is_valid_range(int i[3]) const {return (i[0] >= 0 && i[1] >= 0 && i[2] >= 0 && i[0] < (int)nx && i[1] < (int)ny && i[2] < (int)nz);}

	void get_xyz(point const &p, int xyz[3]) const { // returns whether or not the point was inside the voxel volume
		UNROLL_3X(xyz[i_] = int((p[i_] - lo_pos[i_])/vsz[i_]);); // convert to voxel space
	}
	bool get_ix(point const &p, unsigned &ix) const { // returns whether or not the point was inside the voxel volume
		int i[3]; // x,y,z
		get_xyz(p, i);
		if (!is_valid_range(i)) return 0;
		ix = i[2] + (i[0] + i[1]*nx)*nz;
		return 1;
	}
	unsigned get_ix(unsigned x, unsigned y, unsigned z) const {
		assert(x < nx && y < ny && z < nz);
		return (z + (x + y*nx)*nz);
	}
	V const &get(unsigned x, unsigned y, unsigned z) const {
		unsigned const ix(get_ix(x, y, z));
		assert(ix < size());
		return operator[](ix);
	}
	V &get_ref(unsigned x, unsigned y, unsigned z) {
		unsigned const ix(get_ix(x, y, z));
		assert(ix < size());
		return operator[](ix);
	}
	void set(unsigned x, unsigned y, unsigned z, V const &val) {
		unsigned const ix(get_ix(x, y, z));
		assert(ix < size());
		operator[](ix) = val;
	}
	point get_pt_at(unsigned x, unsigned y, unsigned z) const {return (point(x, y, z)*vsz + lo_pos);}
};

typedef voxel_grid<float> float_voxel_grid;


class voxel_manager : public float_voxel_grid {

protected:
	voxel_params_t params;
	voxel_grid<unsigned char> outside;
	vector<unsigned> first_zval_above_mesh;

	point interpolate_pt(float isolevel, point const &pt1, point const &pt2, float const val1, float const val2) const;
	void atten_edge_val(unsigned x, unsigned y, unsigned z, float val);
	void atten_top_val (unsigned x, unsigned y, unsigned z, float val);
	void calc_outside_val(unsigned x, unsigned y, unsigned z);
	void remove_unconnected_outside_range(bool keep_at_edge, unsigned x1, unsigned y1, unsigned x2, unsigned y2);
	void get_triangles_for_voxel(vector<triangle> &triangles, unsigned x, unsigned y, unsigned z) const;

public:
	void set_params(voxel_params_t const &p) {params = p;}
	void clear();
	void create_procedural(float mag, float freq, vector3d const &offset, bool normalize_to_1, int rseed1, int rseed2);
	void atten_at_edges(float val);
	void atten_at_top_only(float val);
	void determine_voxels_outside();
	void remove_unconnected_outside();
	void create_triangles(vector<triangle> &triangles) const;
	void get_triangles(vector<triangle> &triangles);
	bool point_inside_volume(point const &pos) const;
};


class noise_texture_manager_t {

	unsigned noise_tid, tsize;
	voxel_manager voxels;

public:
	noise_texture_manager_t() : noise_tid(0), tsize(0) {}
	void setup(unsigned size, int rseed=321, float mag=1.0, float freq=1.0, vector3d const &offset=zero_vector);
	void bind_texture(unsigned tu_id) const;
	void clear();
	float eval_at(point const &pos) const;
};


class voxel_model : public voxel_manager {

	bool add_cobjs;
	int last_block_updated;
	typedef vert_norm vertex_type_t;
	typedef vntc_vect_block_t<vertex_type_t> tri_data_t;
	tri_data_t tri_data;
	noise_texture_manager_t noise_tex_gen;
	std::set<unsigned> modified_blocks;
	voxel_grid<float> ao_lighting;

	struct step_dir_t {
		int dir[3];
		unsigned nsteps;
		step_dir_t(int x, int y, int z, unsigned n) : nsteps(n) {dir[0] = x; dir[1] = y; dir[2] = z;}
	};
	vector<step_dir_t> ao_dirs;

	struct data_block_t {
		vector<int> cids; // references into coll_objects
		//unsigned tri_data_ix;
		void clear() {cids.clear();}
	};
	vector<data_block_t> data_blocks;

	struct pt_ix_t {
		point pt;
		unsigned ix;
		pt_ix_t(point const &pt_=all_zeros, unsigned const ix_=0) : pt(pt_), ix(ix_) {}
	};

	vector<pt_ix_t> pt_to_ix;

	struct comp_by_dist {
		point const p;
		comp_by_dist(point const &p_) : p(p_) {}
		bool operator()(pt_ix_t const &a, pt_ix_t const &b) const {return (p2p_dist_sq(a.pt, p) < p2p_dist_sq(b.pt, p));}
	};

	void remove_unconnected_outside_block(unsigned block_ix);
	unsigned get_block_ix(unsigned voxel_ix) const;
	bool clear_block(unsigned block_ix);
	unsigned create_block(unsigned block_ix, bool first_create);
	void calc_ao_dirs();
	void calc_ao_lighting_for_block(unsigned block_ix, bool increase_only);
	void calc_ao_lighting();

public:
	voxel_model() : add_cobjs(0), last_block_updated(-1) {}
	void clear();
	bool update_voxel_sphere_region(point const &center, float radius, float val_at_center, int shooter, unsigned num_fragments=0);
	void proc_pending_updates();
	void build(bool add_cobjs_);
	void render(bool is_shadow_pass);
	void free_context();
	float eval_noise_texture_at(point const &pos) const;
	float get_ao_lighting_val(point const &pos) const;
};

#endif

