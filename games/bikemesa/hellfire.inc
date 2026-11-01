/* Hellfire: an obsidian causeway above a deep, luminous volcanic basin. */
static bool hell_live;
static t3_object *hell_wave,*hell_head,*hell_balls[3];
static t3_material *hell_lava,*hell_warning[3];
static t3_texture *hell_lava_tx;
static cw_node hell_flames[3],hell_burn[MAX_RIDERS],hell_lights[3];
static float hell_time,hell_visual_time;
static int32_t hell_visual_balls;
static const char *const hell_flame_kv[]={"amount","150","lifetime",".65","lifetime_random",".4",
 "emission_shape","SPHERE","emission_radius",".30","direction","vec3(0,1,0)","spread","30",
 "speed_min",".6","speed_max","2.1","gravity","vec3(0,2,0)","turbulence","1.8","turbulence_scale",".35",
 "size",".23","size_random",".6","draw_mode","STRETCHED","stretch",".18","blend","ADD","energy","2.2",
 "size_curve","\"0 .55, .2 1, 1 0\"","color_ramp","\"0 1 .65 .08 0, .12 1 .38 .025 .9, .55 1 .08 .003 .5, 1 .2 .01 0 0\"",NULL};
/* Authored volcanic surface, shaded through the project helllava extension. */
static t3_texture *hell_lava_texture(void) {
 size_t len=0;void *bytes=cw_asset_load("hellfire/molten-basalt.png",&len);
 t3_texture *tx=bytes?t3_texture_load_memory(bytes,len):NULL;free(bytes);
 if(tx){tx->wrap_s=tx->wrap_t=T3_REPEAT;tx->generate_mipmaps=true;tx->min_filter=T3_LINEAR_MIPMAP_LINEAR;tx->color_space=T3_SRGB_COLOR_SPACE;}
 return tx;
}
static void hell_lava_fill(t3_extension_ctx *ctx,void *user) {
 (void)user;float clock[]={hell_time,0,0,0};
 t3_extension_set_uniform(ctx,"hellClock",clock,4);
 t3_extension_set_texture(ctx,"hellSurface",hell_lava_tx);
}
static void hell_lava_register(void) {
 t3_renderer *r=cw_render3d_renderer(cw_scene_app_tree());
 if(r)cw_shading_part(r,"helllava",hell_lava_fill,NULL);
}
#define HELL_LIQUID_PARTS 16
typedef struct {float x,y,z,rx,rz;} hell_pool_desc;
static hell_pool_desc hell_pools[96];static int hell_pool_count;
static t3_object *hell_bubbles[HELL_LIQUID_PARTS],*hell_rafts[HELL_LIQUID_PARTS],*hell_ripples[HELL_LIQUID_PARTS];
static float hell_bubble_phase[HELL_LIQUID_PARTS];
static cw_node hell_splashes[MAX_RIDERS];
static t3_vec3 hell_light_positions[3];
static int hell_nearest_pool(float x,int skip) {
 int best=-1;float distance=1e9f;
 for(int i=0;i<hell_pool_count;i++)if(i!=skip){float d=fabsf(x-hell_pools[i].x)+fabsf(hell_pools[i].z)*.25f;if(d<distance){best=i;distance=d;}}
 return best;
}
static float hell_wave_height(float x,float z,float t) {
 return .10f*sinf(.83f*x+.6f*sinf(.6f*z+.22f*t)-.48f*t)+.08f*sinf(.38f*x+.54f*z+.31f*t)+.035f*sinf(1.7f*z-.25f*x-.65f*t);
}
static void hell_liquid_build(cw_node parent,t3_geometry *slab,t3_material *crust) {
 cw_node n=cw_node_new("Node3D","Living lava");cw_node_add_child(parent,n);
 t3_object *root=t3_group_new();cw_node3d_set_object(n,root);
 t3_geometry *dome=t3_sphere_geometry_new(1,20,12),*ring=t3_torus_geometry_new(1,.035f,6,40,6.2831853f);
 for(int i=0;i<HELL_LIQUID_PARTS;i++) {
  hell_bubbles[i]=&add(root,dome,hell_lava,false)->base;
  hell_rafts[i]=&add(root,slab,crust,true)->base;
  hell_ripples[i]=&add(root,ring,hell_lava,false)->base;
  hell_bubble_phase[i]=0;
 }
 t3_release(dome);t3_release(ring);t3_release(root);
 static const char *const spray[]={"amount","18","lifetime",".65","one_shot","true","emission_shape","SPHERE","emission_radius",".12","direction","vec3(0,1,0)","spread","48","speed_min","1.3","speed_max","3.1","gravity","vec3(0,-8,0)","size",".065","size_random",".5","draw_mode","STRETCHED","stretch",".035","blend","ADD","energy","2","color_ramp","\"0 1 .6 .035 1, .35 1 .25 .003 .9, 1 .25 .005 0 0\"",NULL};
 for(int i=0;i<MAX_RIDERS;i++){hell_splashes[i]=particles(parent,"Molten bubble splash",spray);cw_node_set(hell_splashes[i],"emitting",cw_val_bool(false));}
}
static void hell_liquid_tick(float rider_x) {
 int riders=bike_riders(),per=riders==1?8:4;
 for(int i=0;i<HELL_LIQUID_PARTS;i++) {
  bool active=i<riders*per;hell_bubbles[i]->visible=hell_rafts[i]->visible=active;hell_ripples[i]->visible=false;if(!active)continue;
  int rider=i/per;float x=rider_x;if(rider){bike_select(rider);x=bike_get()->x;bike_select(0);}
  int pi=hell_nearest_pool(x,-1);if(i&1)pi=hell_nearest_pool(x,pi);if(pi<0)continue;
  hell_pool_desc p=hell_pools[pi];float seed=i*2.399963f;
  float bx=p.x+sinf(seed+pi)*p.rx*.52f,bz=p.z+cosf(seed*1.3f+pi)*p.rz*.48f;
  float u=fmodf(hell_time*.23f+i*.173f,1),growth=cw_smoothstep(0,.72f,u),collapse=1-cw_smoothstep(.72f,.95f,u);
  float r=(.35f+.45f*growth)*collapse,h=(.08f+.58f*growth)*collapse;
  float surface=p.y+hell_wave_height(bx,bz,hell_time);
  hell_bubbles[i]->position=(t3_vec3){bx,surface-h*.5f,bz};hell_bubbles[i]->scale=(t3_vec3){r,h,r};
  if(u>.72f&&u<.95f){float q=(u-.72f)/.23f,rr=.65f+q*1.25f;hell_ripples[i]->visible=true;hell_ripples[i]->position=(t3_vec3){bx,surface+.015f,bz};hell_ripples[i]->scale=(t3_vec3){rr,rr,.55f*(1-q)};t3_object_set_rotation(hell_ripples[i],1.5707963f,0,0);}
  if(u>.72f&&hell_bubble_phase[i]<=.72f){cw_node3d_set_position(hell_splashes[rider],(t3_vec3){bx,surface+.12f,bz});cw_particles_restart(hell_splashes[rider]);}
  hell_bubble_phase[i]=u;
  float rx=p.x+cosf(seed*.8f+hell_time*.055f)*p.rx*.58f,rz=p.z+sinf(seed+hell_time*.045f)*p.rz*.53f;
  float ry=p.y+hell_wave_height(rx,rz,hell_time);
  hell_rafts[i]->position=(t3_vec3){rx,ry+.055f,rz};hell_rafts[i]->scale=(t3_vec3){.42f+(i%3)*.22f,.10f,.35f+(i%2)*.18f};
  float dx=(hell_wave_height(rx+.1f,rz,hell_time)-hell_wave_height(rx-.1f,rz,hell_time))*5;
  float dz=(hell_wave_height(rx,rz+.1f,hell_time)-hell_wave_height(rx,rz-.1f,hell_time))*5;
  t3_object_set_rotation(hell_rafts[i],dz,seed+hell_time*.035f,-dx);
 }
}

static void hell_pool(t3_object *root,float x,float y,float z,float rx,float rz,t3_material *lava) {
 if(y<30 && hell_pool_count<96)hell_pools[hell_pool_count++]=(hell_pool_desc){x,y,z,rx,rz};
 mb b={0};enum{R=18,S=96};
 for(int r=0;r<=R;r++)for(int j=0;j<=S;j++) {
  float a=j*6.2831853f/S,u=(float)r/R,edge=1+.09f*sinf(a*5+x)+.045f*cosf(a*11);
  float xx=cosf(a)*rx*u*edge,zz=sinf(a)*rz*u*edge;
  mb_v(&b,(t3_vec3){x+xx,y+.035f*sinf(xx*2+zz)*u,z+zz},(t3_vec3){0,1,0},xx/9,zz/9,1);
 }
 for(int r=0;r<R;r++)for(int j=0;j<S;j++){int a=r*(S+1)+j;mb_t(&b,a,a+S+1,a+1);mb_t(&b,a+1,a+S+1,a+S+2);}
 t3_geometry *g=mb_done(&b);add(root,g,lava,false);t3_release(g);
}
/* Continuous swept anatomy. A single indexed surface replaces stacked tubes. */
typedef struct hell_knot {t3_vec3 p;float r;} hell_knot;
static t3_vec3 hell_curve(const hell_knot *k,int count,float t) {
 float f=cw_clampf(t,0,.999999f)*(count-1);int i=(int)f;float u=f-i;
 t3_vec3 a=k[i?i-1:0].p,b=k[i].p,c=k[i+1].p,d=k[i+2<count?i+2:count-1].p;
 float *aa=&a.x,*bb=&b.x,*cc=&c.x,*dd=&d.x;t3_vec3 p;float *v=&p.x;
 for(int j=0;j<3;j++)v[j]=.5f*((2*bb[j])+(-aa[j]+cc[j])*u+(2*aa[j]-5*bb[j]+4*cc[j]-dd[j])*u*u+(-aa[j]+3*bb[j]-3*cc[j]+dd[j])*u*u*u);
 return p;
}
static void hell_sweep(t3_object *root,t3_material *m,const hell_knot *k,int count,float flatten,bool ridged) {
 mb b={0};enum{ROWS=64,COLS=24};
 for(int i=0;i<=ROWS;i++) {
  float u=i/(float)ROWS,f=cw_clampf(u,0,.999999f)*(count-1);int n=(int)f;
  float r=k[n].r+(k[n+1].r-k[n].r)*cw_smoothstep(0,1,f-n);
  if(ridged)r*=1+.035f*sinf(u*170)*sinf(u*3.1415927f);
  t3_vec3 p=hell_curve(k,count,u),a=hell_curve(k,count,fmaxf(0,u-.002f)),c=hell_curve(k,count,fminf(1,u+.002f));
  t3_vec3 t={c.x-a.x,c.y-a.y,c.z-a.z};float len=sqrtf(t.x*t.x+t.y*t.y+t.z*t.z);t=(t3_vec3){t.x/len,t.y/len,t.z/len};
  t3_vec3 e={t.y,-t.x,0};len=sqrtf(e.x*e.x+e.y*e.y);if(len<.001f)e=(t3_vec3){1,0,0};else {e.x/=len;e.y/=len;}
  t3_vec3 v={t.y*e.z-t.z*e.y,t.z*e.x-t.x*e.z,t.x*e.y-t.y*e.x};
  for(int j=0;j<=COLS;j++){float q=j*6.2831853f/COLS,cs=cosf(q),sn=sinf(q)*flatten;
   mb_v(&b,(t3_vec3){p.x+r*(e.x*cs+v.x*sn),p.y+r*(e.y*cs+v.y*sn),p.z+r*(e.z*cs+v.z*sn)},(t3_vec3){e.x*cs+v.x*sn,e.y*cs+v.y*sn,e.z*cs+v.z*sn},j/(float)COLS,u,1);
  }
 }
 for(int i=0;i<ROWS;i++)for(int j=0;j<COLS;j++){int a=i*(COLS+1)+j;mb_t(&b,a,a+1,a+COLS+1);mb_t(&b,a+1,a+COLS+2,a+COLS+1);}
 t3_geometry *g=mb_done(&b);t3_geometry_compute_vertex_normals(g);add(root,g,m,true);t3_release(g);
}
static float hell_gauss(float x,float center,float width){float d=(x-center)/width;return expf(-d*d);}
static void hell_sculpt(t3_object *root,t3_material *skin,bool head) {
 mb b={0};enum{R=80,C=96};
 for(int i=0;i<=R;i++)for(int j=0;j<=C;j++) {
  float u=i/(float)R,a=j*6.2831853f/C,y=head?3.00f+u*.66f:1.65f+u*1.38f;
  float w,zr;
  if(head){w=.16f+.10f*sinf(u*3.1415927f);w*=cw_smoothstep(0,.12f,u)*sqrtf(fmaxf(.0001f,1-cw_smoothstep(.72f,1,u)));zr=.16f+.045f*sinf(u*3.1415927f);}
  else{w=.29f+.24f*hell_gauss(y,2.63f,.31f)-.17f*cw_smoothstep(2.72f,3.03f,y);zr=.18f+.045f*hell_gauss(y,2.55f,.4f);}
  float x=w*cosf(a),z=zr*sinf(a),front=powf(fmaxf(0,sinf(a)),4);
  if(head) {
   z+=front*(.10f*hell_gauss(x,0,.046f)*hell_gauss(y,3.30f,.10f)
    +.05f*hell_gauss(fabsf(x),.15f,.08f)*hell_gauss(y,3.23f,.08f)
    +.055f*hell_gauss(y,3.44f,.04f)-.045f*hell_gauss(fabsf(x),.12f,.065f)*hell_gauss(y,3.36f,.04f)
    +.06f*hell_gauss(y,3.08f,.06f));
   z+=.006f*sinf(y*140+x*50)*front*hell_gauss(y,3.50f,.12f);
  } else {
   z+=front*(.09f*hell_gauss(fabsf(x),.21f,.17f)*hell_gauss(y,2.59f,.14f)
    +.025f*hell_gauss(fabsf(x),.10f,.08f)*(hell_gauss(y,2.27f,.09f)+hell_gauss(y,2.08f,.085f)+hell_gauss(y,1.90f,.08f))
    -.014f*hell_gauss(x,0,.026f));
  }
  mb_v(&b,(t3_vec3){x,y,z},(t3_vec3){cosf(a),0,sinf(a)},j/(float)C,u,1);
 }
 for(int i=0;i<R;i++)for(int j=0;j<C;j++){int a=i*(C+1)+j;mb_t(&b,a,a+C+1,a+1);mb_t(&b,a+1,a+C+1,a+C+2);}
 t3_geometry *g=mb_done(&b);t3_geometry_compute_vertex_normals(g);add(root,g,skin,true);t3_release(g);
}
static void hell_devil(cw_node parent,t3_object *root,t3_geometry *ball,t3_geometry *cyl,t3_material *rock,float host_x) {
 float x=host_x,y=track_height_at(x,NULL),z=-11;
 t3_geometry *pedestal=rock_geo(ROCK_BLOCK,382);prop(root,pedestal,rock,x,y+.0f,z,3.0f,2.5f,2.7f);t3_release(pedestal);
 t3_object *d=t3_group_new();d->position=(t3_vec3){x,y+1.95f,z};d->scale=(t3_vec3){1.50f,1.50f,1.50f};
 cw_node n=cw_node_new("Node3D","Friendly horned host");cw_node_add_child(parent,n);t3_object *holder=t3_group_new();t3_object_add(holder,d);cw_node3d_set_object(n,holder);t3_release(holder);
 t3_material *skin=stunt_color(.48f,.055f,.040f,0),*horn=stunt_color(.035f,.022f,.018f,0),*cloak=stunt_color(.055f,.035f,.045f,0);
 t3_material *gold=stunt_color(.54f,.32f,.10f,0),*eyes=stunt_color(.9f,.45f,.10f,.25f),*ivory=stunt_color(.62f,.47f,.28f,0);
 skin->roughness=.68f;skin->metalness=0;horn->roughness=.38f;cloak->roughness=.88f;cloak->side=T3_DOUBLE_SIDE;gold->metalness=.8f;gold->roughness=.34f;
 hell_sculpt(d,skin,false);
 t3_geometry *belt=t3_torus_geometry_new(.31f,.045f,12,64,6.2831853f);t3_mesh *buckle=prop(d,belt,gold,0,1.69f,0,1,1,.62f);t3_object_set_rotation(&buckle->base,1.5707963f,0,0);t3_release(belt);
 prop(d,ball,gold,0,1.69f,.22f,.10f,.13f,.025f);
 /* Heavy brocade robes: a continuous pleated lower body, open cape behind. */
 mb robe={0};enum{RR=42,RC=96};
 for(int i=0;i<=RR;i++)for(int j=0;j<=RC;j++) {
  float u=i/(float)RR,a=j*6.2831853f/RC,r=.58f-.27f*u+.035f*sinf(a*16+u*1.7f)*(1-u*.6f);
  mb_v(&robe,(t3_vec3){cosf(a)*r,.13f+u*1.57f,sinf(a)*r*.73f},(t3_vec3){cosf(a),0,sinf(a)},j/12.f,u*3,1);
 }
 for(int i=0;i<RR;i++)for(int j=0;j<RC;j++){int a=i*(RC+1)+j;mb_t(&robe,a,a+RC+1,a+1);mb_t(&robe,a+1,a+RC+1,a+RC+2);}
 t3_geometry *rg=mb_done(&robe);t3_geometry_compute_vertex_normals(rg);add(d,rg,cloak,true);t3_release(rg);
 mb cape={0};enum{CR=42,CC=64};
 for(int r=0;r<=CR;r++)for(int c=0;c<=CC;c++) {
  float u=(float)c/CC,v=(float)r/CR;
  float xx=(u-.5f)*(1.10f+1.6f*v),yy=2.78f-v*(2.6f+.07f*sinf(u*19)),zz=-.15f-.48f*v+.10f*sinf(u*44+v)*(v+.2f);
  mb_v(&cape,(t3_vec3){xx,yy,zz},(t3_vec3){0,.1f,1},u*5,v*5,1);
 }
 for(int r=0;r<CR;r++)for(int c=0;c<CC;c++){int a=r*(CC+1)+c;mb_t(&cape,a,a+1,a+CC+1);mb_t(&cape,a+1,a+CC+2,a+CC+1);}
 t3_geometry *cg=mb_done(&cape);t3_geometry_compute_vertex_normals(cg);add(d,cg,cloak,true);t3_release(cg);
 for(int side=-1;side<=1;side+=2) {
  prop(d,ball,horn,side*.23f,.09f,.10f,.20f,.10f,.31f);
  /* Rounded deltoids bridge the swept arms into the torso without open seams. */
  prop(d,ball,skin,side*.43f,2.64f,0,.20f,.19f,.19f);
  /* Armoured shoulder clasps tie the cape into the anatomy. */
  prop(d,ball,gold,side*.48f,2.70f,-.04f,.13f,.065f,.19f);
  hell_knot seam[]={{{side*.50f,2.72f,-.10f},.018f},{{side*.74f,1.85f,-.30f},.015f},{{side*1.28f,.15f,-.60f},.01f}};
  hell_sweep(d,gold,seam,3,1,false);
 }
 hell_knot arm[]={{{-.43f,2.66f,0},.18f},{{-.63f,2.49f,.07f},.155f},{{-.78f,2.35f,.14f},.10f},{{-.92f,2.60f,.20f},.13f},{{-1.0f,2.84f,.23f},.075f}};
 hell_sweep(d,skin,arm,5,.90f,false);prop(d,ball,skin,-1.0f,2.90f,.23f,.10f,.14f,.08f);
 hell_knot staff[]={{{-1.0f,.05f,.24f},.024f},{{-1.0f,2.5f,.24f},.024f},{{-1.0f,4.02f,.24f},.019f}};
 hell_sweep(d,gold,staff,3,1,false);
 t3_geometry *ring=t3_torus_geometry_new(.14f,.023f,12,48,6.2831853f);prop(d,ring,gold,-1,4.06f,.24f,1,1,1);t3_release(ring);
 /* Wave pivots at the shoulder; a relaxed open palm with separate digits. */
 hell_wave=t3_group_new();hell_wave->position=(t3_vec3){.44f,2.65f,0};t3_object_add(d,hell_wave);t3_release(hell_wave);
 hell_knot wa[]={{{0,0,0},.17f},{{.22f,.06f,.03f},.15f},{{.40f,.22f,.07f},.095f},{{.46f,.43f,.10f},.12f},{{.48f,.66f,.12f},.068f}};
 hell_sweep(hell_wave,skin,wa,5,.9f,false);prop(hell_wave,ball,skin,.48f,.76f,.12f,.095f,.135f,.055f);
 for(int j=0;j<4;j++) {
  float x0=.41f+j*.045f,h=.17f-.026f*abs(j-1);
  hell_knot finger[]={{{x0,.82f,.12f},.024f},{{x0+(j-1.5f)*.015f,.82f+h*.65f,.13f},.022f},{{x0+(j-1.5f)*.025f,.82f+h,.10f},.014f}};
  hell_sweep(hell_wave,skin,finger,3,1,false);
  hell_knot nail[]={{{finger[2].p.x,finger[2].p.y,.10f},.015f},{{finger[2].p.x,finger[2].p.y+.045f,.055f},.001f}};
  hell_sweep(hell_wave,horn,nail,2,.5f,false);
 }
 hell_knot thumb[]={{{.40f,.70f,.12f},.032f},{{.30f,.76f,.12f},.025f},{{.27f,.85f,.08f},.006f}};hell_sweep(hell_wave,skin,thumb,3,1,false);
 hell_head=t3_group_new();t3_object_add(d,hell_head);t3_release(hell_head);hell_sculpt(hell_head,skin,true);
 for(int side=-1;side<=1;side+=2) {
  hell_knot hk[]={{{side*.19f,3.55f,-.02f},.155f},{{side*.40f,3.66f,-.06f},.145f},{{side*.69f,3.94f,-.14f},.088f},{{side*.87f,4.32f,-.21f},.035f},{{side*.92f,4.64f,-.26f},.001f}};
  hell_sweep(hell_head,horn,hk,5,1,true);
  hell_knot ear[]={{{side*.22f,3.35f,-.01f},.075f},{{side*.34f,3.39f,-.08f},.05f},{{side*.42f,3.47f,-.12f},.001f}};hell_sweep(hell_head,skin,ear,3,.45f,false);
  t3_mesh *eye=prop(hell_head,ball,horn,side*.115f,3.355f,.18f,.075f,.028f,.025f);t3_object_set_rotation(&eye->base,0,0,side*.22f);
  eye=prop(hell_head,ball,eyes,side*.115f,3.355f,.20f,.043f,.012f,.008f);t3_object_set_rotation(&eye->base,0,0,side*.22f);
  prop(hell_head,ball,horn,side*.115f,3.355f,.209f,.006f,.013f,.003f);
 }
 /* A narrow, asymmetric half-smile beneath the angular cheekbones. */
 hell_knot smile[]={{{-.115f,3.145f,.181f},.009f},{{-.045f,3.125f,.217f},.011f},{{.05f,3.13f,.215f},.01f},{{.12f,3.16f,.18f},.004f}};hell_sweep(hell_head,horn,smile,4,.5f,false);
 for(int side=-1;side<=1;side+=2){hell_knot tooth[]={{{side*.065f,3.138f,.218f},.012f},{{side*.061f,3.105f,.220f},.001f}};hell_sweep(hell_head,ivory,tooth,2,.6f,false);}
 t3_release(skin);t3_release(horn);t3_release(cloak);t3_release(gold);t3_release(eyes);t3_release(ivory);t3_release(d);
}
static float hell_cliff_height(float x,float iz,int layer) {
 float crest=16+layer*22+20*noise3(x*.025f,layer,0,472)+8*noise3(x*.09f,layer,0,821);
 float u=iz/24.f,profile=cw_smoothstep(0,.25f,u)*(1-.25f*cw_smoothstep(.8f,1,u));
 return -22+profile*crest+profile*(5*noise3(x*.24f,iz*.16f,layer,337)+2*noise3(x*.7f,iz*.4f,layer,111));
}
/* Rounded, fractured basalt with enough surface detail for close foreground use. */
static t3_geometry *hell_boulder(void) {
 t3_geometry *g=t3_sphere_geometry_new(1,32,20);
 t3_attribute *a=g->attributes[T3_ATTR_POSITION];float *p=a->array;
 for(int i=0;i<a->count;i++) {
  float x=p[i*3],y=p[i*3+1],z=p[i*3+2];
  float r=.85f+.22f*noise3(x*2,y*2,z*2,109)+.08f*noise3(x*7,y*7,z*7,881)+.025f*noise3(x*20,y*20,z*20,339);
  p[i*3]=x*r;p[i*3+1]=y*r;p[i*3+2]=z*r;
 }
 a->version++;t3_geometry_compute_vertex_normals(g);return g;
}
static void hellfire_build(cw_node parent,t3_object *root,const track_desc *t) {
 if(strcmp(t->theme,"hellfire"))return;hell_live=true;hell_time=0;hell_pool_count=0;memset(hell_light_positions,0,sizeof hell_light_positions);
 cw_debug_f32("hellfire.visual_time",&hell_visual_time);cw_debug_i32("hellfire.visual_balls",&hell_visual_balls);
 t3_geometry *ball=t3_sphere_geometry_new(1,24,16),*column=t3_cylinder_geometry_new(1,1,1,7,1,false),*cyl=t3_cylinder_geometry_new(1,1,1,14,1,false);
 t3_geometry *crag=hell_boulder(),*slab=rock_geo(ROCK_SLAB,884);
 t3_material *rock=mat(TEX_BASALT,(float[3]){.24f,.23f,.25f},2,2,.8f,.65f);
 t3_material *obsidian=mat(TEX_BASALT,(float[3]){.08f,.065f,.07f},2,2,.6f,.7f);obsidian->metalness=.35f;obsidian->roughness=.48f;
 hell_lava_tx=hell_lava_texture();hell_lava=t3_mesh_standard_material_new(0xffffff);hell_lava->side=T3_DOUBLE_SIDE;hell_lava->metalness=.22f;hell_lava->roughness=1;hell_lava->emissive=(t3_color){1,1,1};hell_lava->emissive_intensity=.42f;
 cw_shading_add(hell_lava,"helllava");hell_lava_register();
 t3_geometry *sky=t3_plane_geometry_new(2400,600,1,1);
 float skycol[]={.006f,.002f,.018f,.006f,.002f,.018f,.12f,.018f,.004f,.12f,.018f,.004f};
 set_attr(sky,T3_ATTR_COLOR,t3_attribute_new(T3_FLOAT32,skycol,4,3));
 t3_material *sm=t3_mesh_basic_material_new(0xffffff);sm->vertex_colors=true;sm->fog=false;
 t3_mesh *sk=add(root,sky,sm,false);sk->base.position=(t3_vec3){180,150,-750};t3_release(sky);t3_release(sm);
 /* Deep background cliffs are actual tiers of rock, silhouetted against haze. */
 for(int layer=3;layer>=0;layer--) {
  mb b={0};int nx=(int)ceilf((t->length+840)/(8.f/3)),nz=24;float z0=-65-layer*90.f;
  for(int iz=0;iz<=nz;iz++)for(int ix=0;ix<=nx;ix++) {
   float x=-420+ix*(8.f/3),z=z0-iz*2;
   float h=hell_cliff_height(x,(float)iz,layer);
   mb_v(&b,(t3_vec3){x,h,z},(t3_vec3){0,1,0},x/10,h/10,.75f+.25f*cosf(x*.63f));
  }
  for(int iz=0;iz<nz;iz++)for(int ix=0;ix<nx;ix++){int a=iz*(nx+1)+ix;mb_t(&b,a,a+1,a+nx+1);mb_t(&b,a+1,a+nx+2,a+nx+1);}
  t3_geometry *g=mb_done(&b);t3_geometry_compute_vertex_normals(g);t3_material *m=mat(TEX_BASALT,(float[3]){.25f+layer*.025f,.23f+layer*.025f,.26f+layer*.025f},1,1,.7f,.85f);add(root,g,m,true);t3_release(g);t3_release(m);
 }
 /* Narrow lava cascades descend the nearest cliff's actual surface. */
 for(int k=0;k<(int)(t->length/106)+1;k++) {
  mb flow={0};float base=30+k*106;
  for(int j=0;j<=64;j++)for(int side=-1;side<=1;side+=2) {
   float iz=j*18.f/64,x=base+1.3f*sinf(iz*.21f)+side*(.45f+.30f*sinf(iz*.3f));
   mb_v(&flow,(t3_vec3){x,hell_cliff_height(x,iz,0)+.23f,-65-iz*2},(t3_vec3){0,1,0},side*.10f,iz*.23f,1);
   if(side==1&&j){int a=j*2-2;mb_t(&flow,a,a+1,a+2);mb_t(&flow,a+1,a+3,a+2);}
  }
  t3_geometry *g=mb_done(&flow);add(root,g,hell_lava,false);t3_release(g);
 }
 /* A distant active caldera gives the long course a recognizable destination. */
 {
  float vx=t->finish_x*.58f,vz=-180;mb b={0};enum{R=32,C=96};
  for(int r=0;r<=R;r++)for(int j=0;j<=C;j++) {
   float u=r/(float)R,a=j*6.2831853f/C;
   float radius=9+75*powf(1-u,1.7f),ridge=1+.035f*sinf(a*13)+.024f*cosf(a*21+u*5);
   float h=-23+u*78+1.1f*sinf(a*9+u*2)*u;
   mb_v(&b,(t3_vec3){vx+cosf(a)*radius*ridge,h,vz+sinf(a)*radius*ridge},(t3_vec3){cosf(a),.7f,sinf(a)},j/8.f,u*8,1);
  }
  for(int r=0;r<R;r++)for(int j=0;j<C;j++){int a=r*(C+1)+j;mb_t(&b,a,a+C+1,a+1);mb_t(&b,a+1,a+C+1,a+C+2);}
  t3_geometry *g=mb_done(&b);t3_geometry_compute_vertex_normals(g);add(root,g,rock,false);t3_release(g);
  hell_pool(root,vx,54,vz,9,9,hell_lava);
  static const char *const smoke[]={"amount","28","lifetime","14","preprocess","10","emission_shape","SPHERE","emission_radius","4","direction","vec3(.25,1,0)","spread","12","speed_min","1.8","speed_max","3","gravity","vec3(.15,.05,0)","turbulence",".4","size","7","size_random",".4","blend","ALPHA","size_curve","\"0 .4, .5 1, 1 1.7\"","color_ramp","\"0 .08 .065 .06 0, .15 .10 .08 .07 .32, .7 .13 .10 .09 .16, 1 .15 .12 .11 0\"",NULL};
  cw_node plume=particles(parent,"Distant caldera plume",smoke);cw_node3d_set_position(plume,(t3_vec3){vx,55,vz});
 }
 /* Irregular lava pools sit above a broad black basin; the eye can see
  * between their shores all the way to the foot of the distant cliffs. */
 for(int i=0;i<(int)(t->length/29)+2;i++) {
  float x=-18+i*29.f,y=ground_at(x)-2.7f,z=-14-(i%3)*10.f;
  hell_pool(root,x,y,z,12+i%3*3,7+i%2*3,hell_lava);
  for(int j=0;j<18;j++){float a=j*6.2831853f/18;float xx=x+cosf(a)*(13+i%3*3),zz=z+sinf(a)*(8+i%2*3);float bottom=fminf(bank_y(xx,zz,5)-.25f,y-.65f),top=y+.38f;prop(root,crag,rock,xx,(bottom+top)*.5f,zz,1.5f,(top-bottom)*.58f,1.2f);}
  for(int j=0;j<9;j++){float a=rnd()*6.2831853f,r=rnd()*.75f; t3_mesh *p=prop(root,slab,obsidian,x+cosf(a)*r*10,y+.06f,z+sinf(a)*r*6,1.2f+rnd()*1.8f,.08f,.5f+rnd());t3_object_set_rotation(&p->base,0,rnd()*6,0);}
 }
 for(int i=0;i<(int)(t->length/7.2f)+8;i++){float x=-35+i*7.2f,z=-44-rnd()*20,y=ground_at(x)-5,h=2+rnd()*6;prop(root,crag,rock,x,y+h*.5f,z,1.2f+rnd()*2,h,1.3f+rnd()*2);}
 for(int i=0;i<(int)(t->length/6)+5;i++){float x=-15+i*6,z=-4.5f-rnd()*2,y=bank_y(x,z,5)-.2f;prop(root,crag,obsidian,x,y,z,.7f+rnd(),.5f+rnd(),.5f+rnd());}
 /* The riding gaps need their own lava, below all four lanes. Background
  * pools cannot communicate the hazard when the foreground bank hides them. */
 for(int gap=1;gap<t->piece_count;gap++) {
  const track_piece *left=&t->pieces[gap-1],*right=&t->pieces[gap];
  float x0=left->pts[(left->count-1)*2],x1=right->pts[0];
  float y0=left->pts[(left->count-1)*2+1],y1=right->pts[1];
  float x=(x0+x1)*.5f,y=fminf(y0,y1)-1.7f,rx=(x1-x0)*.5f+3;
  hell_pool(root,x,y,-1,rx,9,hell_lava);
  /* Broken basalt lips frame the pool; low silhouettes keep the liquid open. */
  for(int j=0;j<13;j++) {
   float xx=x0-1+(x1-x0+2)*j/12.f,zz=5.9f+.35f*sinf(j*2.7f);
   prop(root,crag,obsidian,xx,y-.05f,zz,.65f+.2f*(j%3),.3f,.6f);
  }
  for(int j=0;j<6;j++) {
   float xx=x0+2+(x1-x0-4)*j/5.f,zz=-.7f+2.6f*sinf(j*2.3f);
   t3_mesh *c=prop(root,slab,obsidian,xx,y+.07f,zz,.4f+(j%3)*.16f,.07f,.35f);
   t3_object_set_rotation(&c->base,0,j*1.7f,0);
  }
  static const char *const sparks[]={"amount","52","lifetime","2.5","emission_shape","BOX","emission_extents","vec3(5,.1,3)","direction","vec3(0,1,0)","spread","24","speed_min",".4","speed_max","1.5","gravity","vec3(.1,.3,0)","turbulence",".4","size",".035","blend","ADD","energy","3","color_ramp","\"0 1 .35 .01 0, .15 1 .55 .035 .9, 1 .5 .015 0 0\"",NULL};
  cw_node e=particles(parent,"Lava pit embers",sparks);cw_node3d_set_position(e,(t3_vec3){x,y+.1f,-1});
 }
 hell_liquid_build(parent,slab,obsidian);
 hell_devil(parent,root,ball,cyl,rock,t->finish_x*.55f);
 /* Three real pools of moving light, with no blanket orange wash. */
 for(int i=0;i<3;i++){hell_lights[i]=cw_node_new("OmniLight3D","Lava reflected light");cw_node_add_child(parent,hell_lights[i]);cw_node_set(hell_lights[i],"color",cw_val_color(1,.16f,.025f,1));cw_node_set(hell_lights[i],"energy",cw_val_float(35));cw_node_set(hell_lights[i],"range",cw_val_float(28));}
 for(int i=0;i<3;i++) {
  t3_object *o=t3_group_new();hell_balls[i]=o;cw_node n=cw_node_new("Node3D","Erupting iron fireball");cw_node_add_child(parent,n);t3_object *holder=t3_group_new();t3_object_add(holder,o);cw_node3d_set_object(n,holder);t3_release(holder);
  t3_material *iron=stunt_color(.22f,.025f,.008f,.6f);iron->metalness=.7f;iron->roughness=.36f;
  prop(o,ball,iron,0,0,0,.43f,.43f,.43f);
  t3_material *seam=stunt_color(1,.12f,.005f,4);t3_geometry *ring=t3_torus_geometry_new(.423f,.013f,6,48,6.2831853f);
  for(int j=0;j<3;j++){t3_mesh *r=prop(o,ring,seam,0,0,0,1,1,1);t3_object_set_rotation(&r->base,j*.9f,j*.7f,j*.4f);}
  t3_release(ring);t3_release(iron);t3_release(seam);t3_release(o);
  hell_flames[i]=particles(parent,"Eruption flames",hell_flame_kv);cw_node_set(hell_flames[i],"emitting",cw_val_bool(false));
  float xs[]={52,t->finish_x*.45f,t->finish_x*.8f};int lanes[]={1,2,1};float y=track_height_at(xs[i],NULL);
  hell_warning[i]=stunt_color(1,.12f,.005f,.3f);t3_geometry *vent=t3_torus_geometry_new(.65f,.08f,8,48,6.2831853f);t3_mesh *v=prop(root,vent,hell_warning[i],xs[i],y+.04f,lane_z(lanes[i]),1,1,1);t3_object_set_rotation(&v->base,1.5707963f,0,0);t3_release(vent);
 }
 for(int i=0;i<MAX_RIDERS;i++){hell_burn[i]=particles(parent,"Rider on fire",hell_flame_kv);cw_node_set(hell_burn[i],"emitting",cw_val_bool(false));}
 static const char *const embers[]={"amount","180","lifetime","6","preprocess","4","emission_shape","BOX","emission_extents","vec3(35,2,16)","direction","vec3(0,1,0)","spread","30","speed_min",".3","speed_max","1.2","gravity","vec3(.2,.25,0)","turbulence",".6","size",".028","blend","ADD","energy","3","color_ramp","\"0 1 .2 .01 0, .2 1 .4 .02 .8, 1 .4 .02 0 0\"",NULL};
 cw_node e=particles(parent,"Basin embers",embers);cw_node3d_set_position(e,(t3_vec3){175,-2,-15});
 t3_release(crag);t3_release(slab);t3_release(ball);t3_release(column);t3_release(cyl);t3_release(rock);t3_release(obsidian);
 /* Keep owning references to mutable materials/texture until view_forget. */
}
static void hellfire_tick(float x) {
 if(!hell_live)return;hell_time=bike_hellfire_time();hell_liquid_tick(x);
 t3_object_set_rotation(hell_wave,0,.10f*sinf(hell_time*.7f),-.20f+.16f*sinf(hell_time*2.4f));
 t3_object_set_rotation(hell_head,0,.08f*sinf(hell_time*.8f),.035f*sinf(hell_time*1.7f));
 /* Flow is driven by hellClock; the source texture stays stable. */
 for(int i=0;i<3;i++) {
  t3_vec3 at;float warning;bool active=bike_hellfire_ball(i,&at,&warning);
  hell_balls[i]->visible=active;hell_balls[i]->position=at;t3_object_set_rotation(hell_balls[i],hell_time*1.2f,hell_time*.7f,hell_time*.5f);
  cw_node3d_set_position(hell_flames[i],at);cw_node_set(hell_flames[i],"emitting",cw_val_bool(active));
  hell_warning[i]->emissive_intensity=.35f+warning*(3+2*sinf(hell_time*25));
  int pool=hell_nearest_pool(x+(i-1)*15,-1);
  if(pool>=0) {
   hell_pool_desc p=hell_pools[pool];t3_vec3 target={p.x,p.y+1.2f,p.z};
   t3_vec3 *at=&hell_light_positions[i];float blend=hell_time<.1f?1:.09f;
   at->x+=(target.x-at->x)*blend;at->y+=(target.y-at->y)*blend;at->z+=(target.z-at->z)*blend;
   cw_node3d_set_position(hell_lights[i],*at);
   cw_node_set(hell_lights[i],"energy",cw_val_float(32+7*sinf(hell_time*1.4f+pool)+3*sinf(hell_time*4.1f+i)));
  }
 }
 for(int i=0;i<MAX_RIDERS;i++){t3_vec3 at;bool burning=bike_hellfire_burn(i,&at);cw_node_set(hell_burn[i],"emitting",cw_val_bool(burning));if(burning)cw_node3d_set_position(hell_burn[i],at);}
}
/* Replay uses the recorded physics clock, never the frozen current world tick.
 * This is also called per split-screen view so other riders keep their live scene. */
void view_hellfire_pose(float time,const bike_pose *pose,int rider) {
 if(!hell_live)return;hell_visual_time=time;hell_visual_balls=0;
 for(int i=0;i<3;i++) {
  t3_vec3 at={0};float warning=0;
  bool active=bike_hellfire_ball_at(i,time,&at,&warning);
  if(active)hell_visual_balls|=1<<i;
  hell_balls[i]->visible=active;hell_balls[i]->position=at;
  t3_object_set_rotation(hell_balls[i],time*1.2f,time*.7f,time*.5f);
  t3_object_update_world_matrix(hell_balls[i],true,true);
  hell_warning[i]->emissive_intensity=.35f+warning*(3+2*sinf(time*25));
  cw_node3d_set_position(hell_flames[i],at);
  cw_node_set(hell_flames[i],"emitting",cw_val_bool(active));
 }
 if(pose&&rider>=0&&rider<MAX_RIDERS) {
  bool burning=pose->burning&&pose->ragdoll;
  cw_node_set(hell_burn[rider],"emitting",cw_val_bool(burning));
  if(burning)cw_node3d_set_position(hell_burn[rider],pose->limb3[RAG_TORSO].p);
 }
}
static void hellfire_forget(void) {
 if(hell_live){t3_release(hell_lava);t3_release(hell_lava_tx);for(int i=0;i<3;i++)t3_release(hell_warning[i]);}
 hell_live=false;hell_wave=hell_head=NULL;hell_lava=NULL;hell_lava_tx=NULL;
}
