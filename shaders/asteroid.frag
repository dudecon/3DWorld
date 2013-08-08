uniform float tscale = 1.0;
uniform float crater_scale = 1.0;
uniform sampler2D tex0;

#ifdef ENABLE_SHADOWS
uniform vec3 sun_pos;
uniform float sun_radius;
uniform vec4 shadow_casters[8];
#endif

varying vec3 vpos, normal, world_normal, world_space_pos;
varying vec4 epos;

void main()
{
	vec4 texel = lookup_triplanar_texture(tscale*vpos, normalize(world_normal), tex0, tex0, tex0);
	vec4 color = gl_FrontMaterial.emission;
	vec3 norm_normal = normalize(normal);

#ifdef HAS_CRATERS
	if (crater_scale > 0.0 && dot(norm_normal, normalize(gl_LightSource[0].position.xyz - epos.xyz)) > 0.0) { // facing the sun
		adjust_normal_for_craters(norm_normal, vpos); // add craters by modifying the normal
	}
#endif

#ifdef ENABLE_SHADOWS
	float atten = 1.0;

	for (int i = 0; i < num_shadow_casters; ++i) {
		atten *= calc_sphere_shadow_atten(world_space_pos, sun_pos, sun_radius, shadow_casters[i].xyz, shadow_casters[i].w);
	}
	norm_normal *= atten;
#endif

	for (int i = 0; i < 2; ++i) { // sun_diffuse, galaxy_ambient
		color += add_pt_light_comp(norm_normal, epos, i);
	}
	gl_FragColor = vec4(texel.rgb * clamp(color.rgb, 0.0, 1.0), texel.a * gl_Color.a); // use diffuse alpha directly;
}

