/* Backyard Giants: a miniature garden at a bug's scale: giant blades, beetles, dragonflies,
 * a hose, a watering can, the fence and the house. Only the beetles and dragonflies animate. */
static void gg_leaf(t3_object *root,t3_material *mat,float x,float y,float z,float h,float bend,float yaw) {
 mb m={0};enum{N=10};
 for(int i=0;i<=N;i++) {
  float u=i/(float)N,w=.65f*sinf(u*3.1415927f)+.04f*(1-u),cx=bend*u*u;
  for(int side=-1;side<=1;side++)mb_v(&m,(t3_vec3){cx+side*w,h*u,(side?.0f:.18f)*sinf(u*3.1415927f)},(t3_vec3){side*.3f,-bend*u/h,1},(side+1)*.5f,u,1);
 }
 for(int i=0;i<N;i++)for(int j=0;j<2;j++){int a=i*3+j;mb_t(&m,a,a+1,a+4);mb_t(&m,a,a+4,a+3);}
 t3_geometry *g=mb_done(&m);t3_mesh *leaf=add(root,g,mat,true);leaf->base.position=(t3_vec3){x,y,z};t3_object_set_rotation(&leaf->base,0,yaw,0);t3_release(g);
}
/* Sculpted elytra: flattened under-edge, domed shoulders, central seam and
 * longitudinal striae. Detail is geometry rather than painted cartoon spots. */
static void gg_elytron(t3_object *root,t3_material *material,int side) {
 mb b={0};enum{NX=64,NA=32};
 for(int i=0;i<=NX;i++)for(int j=0;j<=NA;j++) {
  float u=i/(float)NX,a=j/(float)NA*3.1415927f*.5f;
  float x=-2.6f+u*4.8f,shape=powf(fmaxf(.0001f,sinf(u*3.1415927f)),.48f);
  float z=side*(.025f+1.40f*shape*sinf(a));
  float ribs=.012f*cosf(a*35)*sinf(u*3.1415927f),pores=.003f*sinf(x*93+z*37)*sinf(z*87);
  float y=.12f+1.10f*shape*cosf(a)+ribs+pores;
  mb_v(&b,(t3_vec3){x,y,z},(t3_vec3){0,cosf(a),side*sinf(a)},u,j/(float)NA,1);
 }
 for(int i=0;i<NX;i++)for(int j=0;j<NA;j++){int a=i*(NA+1)+j;if(side<0){mb_t(&b,a,a+NA+1,a+1);mb_t(&b,a+1,a+NA+1,a+NA+2);}else{mb_t(&b,a,a+1,a+NA+1);mb_t(&b,a+1,a+NA+2,a+NA+1);}}
 t3_geometry *g=mb_done(&b);t3_geometry_compute_vertex_normals(g);add(root,g,material,true);t3_release(g);
}
static void gg_beetle_leg(t3_object *root,t3_material *m,int side,int leg) {
 float x=-1.45f+leg*1.5f,s=side;
 hell_knot k[]={{{x,.1f,s*.85f},.22f},{{x+.2f,.0f,s*1.35f},.18f},{{x-.38f,.25f,s*2.0f},.14f},{{x-.9f,-.60f,s*2.35f},.09f},{{x-1.18f,-1.35f,s*2.65f},.065f},{{x-.82f,-1.45f,s*2.95f},.035f}};
 hell_sweep(root,m,k,6,.72f,false);
 for(int claw=-1;claw<=1;claw+=2){hell_knot tip[]={{{x-.82f,-1.45f,s*2.95f},.04f},{{x-.62f,-1.46f,s*(3.03f+claw*.08f)},.025f},{{x-.50f,-1.40f,s*(3.02f+claw*.12f)},.002f}};hell_sweep(root,m,tip,3,1,false);}
}
static void backyard_build(cw_node parent,t3_object *root,const track_desc *t) {
 if(strcmp(t->theme,"backyard"))return;
 t3_geometry *ball=t3_sphere_geometry_new(1,40,28),*box=t3_box_geometry_new(1,1,1,1,1,1),*cyl=t3_cylinder_geometry_new(1,1,1,10,1,false);
 t3_material *greens[3];for(int i=0;i<3;i++){greens[i]=stunt_color(.10f+i*.045f,.28f+i*.08f,.025f+i*.012f,0);greens[i]->side=T3_DOUBLE_SIDE;greens[i]->roughness=.75f;}
 t3_material *wood=mat(TEX_WOOD,(float[3]){.55f,.37f,.20f},1,1,1,.8f),*dark=stunt_color(.025f,.018f,.012f,0),*yellow=stunt_color(.95f,.57f,.035f,0),*red=stunt_color(.65f,.025f,.012f,0),*shell=stunt_color(.025f,.075f,.038f,0),*silver=stunt_color(.42f,.48f,.48f,0);
 shell->metalness=.68f;shell->roughness=.28f;dark->roughness=.31f;red->color=lin(.12f,.035f,.012f);red->metalness=.35f;red->roughness=.33f;silver->metalness=.75f;silver->roughness=.27f;
 /* Individual curved blades: their huge silhouettes establish the rider's scale. */
 for(int i=0;i<(int)(t->length/3);i++) {
  float x=-30+i*3.2f,z=-9-rnd()*45,y=bank_y(x,z,6);
  for(int j=0;j<3;j++)gg_leaf(root,greens[(i+j)%3],x+j*.7f,y,z,7+rnd()*13,(rnd()-.5f)*8,rnd()*6.28f);
 }
 /* A full-height garden fence and the distant house give depth beyond the grass. */
 for(float x=-80;x<t->length+100;x+=9){float y=ground_at(x)-7;prop(root,box,wood,x,y+14,-65,7.9f,28,.8f);prop(root,box,dark,x,y+27.8f,-64.5f,7.7f,.16f,.12f);}
 for(int k=0;k<3;k++) {
  float x=170+k*410,y=ground_at(x),z=-95;
  prop(root,box,wood,x,y+40,z,90,70,35);prop(root,box,dark,x,y+78,z,105,6,48);
  for(int j=-1;j<=1;j++){prop(root,box,silver,x+j*23,y+42,z+18,14,22,.4f);prop(root,box,dark,x+j*23,y+42,z+18.3f,.7f,22,.2f);}
 }
 for(int i=0;i<8;i++) {
  float x=105+i*165,y=bank_y(x,-17,6),h=18+(i%3)*3;
  lg_stick(root,cyl,greens[1],(t3_vec3){x,y,-17},(t3_vec3){x+2,y+h,-17},.38f);
  prop(root,ball,dark,x+2,y+h,-17,1.8f,1.8f,.65f);
  for(int j=0;j<13;j++){float a=j*6.2831853f/13;t3_mesh *p=prop(root,ball,yellow,x+2+cosf(a)*2.9f,y+h+sinf(a)*2.9f,-17,1.8f,.65f,.32f);t3_object_set_rotation(&p->base,0,0,a);}
  gg_leaf(root,greens[2],x,y+h*.35f,-17,5,5,.4f);
 }
 /* Ground beetles with protective wing cases, thorax, articulated tarsi and
  * mouthparts. Their feet remain planted rather than bouncing like toys. */
 for(int i=0;i<6;i++) {
  float x=115+i*205,y=bank_y(x,-8,6);t3_object *bug=t3_group_new();bug->position=(t3_vec3){x,y+1.45f,-8};
  prop(bug,ball,dark,-.3f,-.05f,0,2.4f,.65f,1.25f);
  gg_elytron(bug,i%3? shell:red,-1);gg_elytron(bug,i%3?shell:red,1);
  prop(bug,ball,shell,2.0f,.22f,0,.8f,.64f,1.05f);
  prop(bug,ball,dark,2.87f,.05f,0,.66f,.42f,.68f);
  for(int side=-1;side<=1;side+=2) {
   prop(bug,ball,dark,3.03f,.21f,side*.60f,.24f,.24f,.16f);
   for(int leg=0;leg<3;leg++)gg_beetle_leg(bug,dark,side,leg);
   hell_knot antenna[]={{{3.12f,.24f,side*.45f},.07f},{{3.8f,.62f,side*.8f},.06f},{{4.4f,1.2f,side*1.6f},.035f},{{4.6f,1.6f,side*2.1f},.009f}};
   hell_sweep(bug,dark,antenna,4,1,true);
   hell_knot jaw[]={{{3.35f,-.03f,side*.32f},.13f},{{3.82f,-.08f,side*.40f},.10f},{{3.98f,-.02f,side*.09f},.005f}};
   hell_sweep(bug,dark,jaw,3,.6f,false);
  }
  merge_still(bug,0,bug->child_count,0);gg_animate(parent,bug,1);t3_release(bug);
 }
 /* A coiled hose and giant watering can: recognizable, not abstract scenery. */
 t3_geometry *ring=t3_torus_geometry_new(8,.55f,12,64,6.2831853f);
 for(int i=0;i<3;i++){float x=330+i*340,y=bank_y(x,-13,6);t3_mesh *r=prop(root,ring,greens[0],x,y+.65f,-14,1,1,1);t3_object_set_rotation(&r->base,1.5707963f,0,0);prop(root,cyl,silver,x+10,y+5,-22,4,10,4);lg_stick(root,cyl,silver,(t3_vec3){x+12,y+4,-22},(t3_vec3){x+19,y+10,-22},1.0f);prop(root,ball,silver,x+19,y+10,-22,2,.6f,2);}
 /* Dragonflies hang above the path; four veined wings and a segmented abdomen. */
 for(int i=0;i<3;i++) {t3_object *fly=t3_group_new();float x=245+i*430;fly->position=(t3_vec3){x,ground_at(x)+13,-5};
  for(int j=0;j<8;j++)prop(fly,ball,shell,-j*.55f,0,0,.4f-j*.025f,.35f-j*.02f,.35f-j*.02f);
  prop(fly,ball,dark,.55f,0,0,.7f,.55f,.6f);
  for(int side=-1;side<=1;side+=2)for(int j=0;j<2;j++){t3_mesh *w=prop(fly,ball,silver,-j*1.1f,.15f,side*2.1f,.65f,.055f,2.4f);t3_object_set_rotation(&w->base,0,side*(j?.3f:-.3f),0);for(int v=0;v<4;v++)lg_stick(fly,cyl,dark,(t3_vec3){-j*1.1f,.22f,0},(t3_vec3){-j*1.1f-.4f+v*.25f,.22f,side*4.0f},.014f);}
  merge_still(fly,0,fly->child_count,0);gg_animate(parent,fly,2);t3_release(fly);
 }
 t3_release(ring);t3_release(ball);t3_release(box);t3_release(cyl);
 for(int i=0;i<3;i++)t3_release(greens[i]);t3_material *ms[]={wood,dark,yellow,red,shell,silver};for(int i=0;i<6;i++)t3_release(ms[i]);
}
