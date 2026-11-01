/* Black Circuit: a luminous black racing circuit under a synthwave horizon.
 * Static scenery uses the dense-level spatial batches. Only hero accents animate. */
static t3_object *gg_motion[24];static t3_vec3 gg_home[24];static int gg_kind[24],gg_count;
static void gg_animate(cw_node parent,t3_object *o,int kind) {
 if(gg_count>=24)return;cw_node n=cw_node_new("Node3D","World accent");cw_node_add_child(parent,n);
 t3_object *holder=t3_group_new();t3_object_add(holder,o);cw_node3d_set_object(n,holder);t3_release(holder);
 gg_motion[gg_count]=o;gg_home[gg_count]=o->position;gg_kind[gg_count++]=kind;
}
static void gg_tick(void) {
 float t=bike_hellfire_time();
 for(int i=0;i<gg_count;i++) {
  t3_object *o=gg_motion[i];t3_vec3 p=gg_home[i];
  if(gg_kind[i]==1){p.y+=.018f*sinf(t*1.3f+i);t3_object_set_rotation(o,0,.018f*sinf(t*.7f+i),.005f*sinf(t*2+i));}
  if(gg_kind[i]==2){p.x+=4*sinf(t*.3f+i);p.y+=1.2f*sinf(t*.9f+i);t3_object_set_rotation(o,.1f*sinf(t*2),0,.06f*sinf(t*3));}
  if(gg_kind[i]==3){t3_object_set_rotation(o,0,0,t*.2f);}
  if(gg_kind[i]==4){p.x+=fmodf(t*18+i*19,110)-55;}
  o->position=p;
 }
}
/* A thin strut between two points, any direction (the neon crests and spokes). */
static void lg_stick(t3_object *root,t3_geometry *cyl,t3_material *m,t3_vec3 a,t3_vec3 b,float radius) {
 t3_vec3 v={b.x-a.x,b.y-a.y,b.z-a.z};float len=sqrtf(v.x*v.x+v.y*v.y+v.z*v.z);
 if(len<.0001f)return;
 t3_mesh *o=prop(root,cyl,m,(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f,radius,len,radius);
 t3_quat q={v.z/len,0,-v.x/len,1+v.y/len};float n=sqrtf(q.x*q.x+q.z*q.z+q.w*q.w);
 if(n<.0001f)q=(t3_quat){1,0,0,0};else{q.x/=n;q.z/=n;q.w/=n;}t3_object_set_quaternion(&o->base,q);
}
static void lightgrid_horizon(cw_node parent,t3_object *root,const track_desc *t,t3_geometry *box,t3_geometry *cyl,t3_material *black,t3_material *cyan,t3_material *pink) {
 /* World-fixed horizon: a kilometre behind the structures, almost stationary
  * relative to the rider. Intermediate ridges and city layers supply parallax. */
 float sx=t->finish_x*.55f,sy=220,sz=-1800,radius=230;
 for(int band=0;band<12;band++) {
  float lo=-1+band/6.f,hi=lo+.135f,mid=(lo+hi)*.5f;
  mb m={0};for(int row=0;row<=8;row++){float yy=lo+(hi-lo)*row/8.f,xx=sqrtf(fmaxf(0,1-yy*yy))*radius;mb_v(&m,(t3_vec3){sx-xx,sy+yy*radius,sz},(t3_vec3){0,0,1},0,0,1);mb_v(&m,(t3_vec3){sx+xx,sy+yy*radius,sz},(t3_vec3){0,0,1},1,0,1);}
  for(int row=0;row<8;row++){int a=row*2;mb_t(&m,a,a+1,a+3);mb_t(&m,a,a+3,a+2);}
  t3_material *sun=stunt_color(1,.035f+.39f*(mid+1)*.5f,.08f+.18f*(1-mid)*.5f,2.5f);
  t3_geometry *g=mb_done(&m);add(root,g,sun,false);t3_release(g);t3_release(sun);
 }
 for(int layer=0;layer<3;layer++) {
  float z=-1150+layer*230;mb m={0};
  t3_material *ridge=stunt_color(.025f+layer*.008f,.006f,.055f+layer*.02f,.35f);
  t3_material *crest=stunt_color(.22f+layer*.08f,.015f,.38f,1.2f);
  for(int i=0;i<=72;i++){float x=-1700+i*70,y=-50+70*sinf(i*.63f+layer)+32*sinf(i*1.7f+layer*4);int a=m.nv;mb_v(&m,(t3_vec3){x,-450,z},(t3_vec3){0,0,1},0,0,1);mb_v(&m,(t3_vec3){x,y,z},(t3_vec3){0,0,1},0,1,1);if(i){mb_t(&m,a-2,a,a+1);mb_t(&m,a-2,a+1,a-1);float px=x-70,py=-50+70*sinf((i-1)*.63f+layer)+32*sinf((i-1)*1.7f+layer*4);lg_stick(root,cyl,crest,(t3_vec3){px,py,z+.1f},(t3_vec3){x,y,z+.1f},.65f);}}
  t3_geometry *g=mb_done(&m);add(root,g,ridge,false);t3_release(g);t3_release(ridge);t3_release(crest);
 }
 /* Mid-distance city silhouettes with branching circuit traces and windows. */
 t3_material *violet=stunt_color(.18f,.035f,.5f,1.4f);
 for(int layer=0;layer<2;layer++)for(int i=0;i<48;i++) {
  float x=-450+i*47,z=-210-layer*210,h=24+64*rnd(),y=-40,width=12+rnd()*13;
  prop(root,box,black,x,y+h*.5f,z,width,h,14);
  prop(root,box,layer? violet:cyan,x-width*.5f,y+h*.5f,z+7.1f,.18f,h,.1f);
  prop(root,box,layer? violet:pink,x,y+h,z+7.1f,width,.18f,.1f);
  for(int w=0;w<8;w++){float yy=y+5+w*h/9;prop(root,box,violet,x+width*.18f,yy,z+7.2f,width*.48f,.4f,.1f);}
 }
 /* Converging floor grid, visible well beyond the road's near structure. */
 for(int i=0;i<25;i++){float z=-45-i*i*1.5f;prop(root,box,violet,t->finish_x*.5f,-58,z,t->length+1500,.055f,.16f);}
 for(float x=-600;x<t->length+700;x+=50)prop(root,box,violet,x,-58,-500,.12f,.055f,1000);
 t3_release(violet);
 /* A dense city canyon occupies the middle third, followed by the reactor. */
 for(int i=0;i<15;i++){float x=t->finish_x*.35f+i*13,y=ground_at(x),h=18+12*rnd();for(int side=0;side<2;side++){float z=-9-side*13;prop(root,box,black,x,y+h*.5f,z,5,h,5);for(int j=0;j<4;j++){float yy=y+2+j*h*.22f;prop(root,box,i%3?cyan:pink,x,yy,z+2.55f,4,.12f,.06f);}prop(root,box,cyan,x+2.5f,y+h*.5f,z+2.55f,.08f,h,.06f);}}
 /* Large reactor ribs surround the loop's space; an open front preserves view. */
 if(t->loop_count){float x=t->loops[0],y=t->loops[1]+t->loops[2],r=t->loops[2]+5;
  t3_geometry *hoop=t3_torus_geometry_new(r,.10f,8,96,6.2831853f);
  for(int i=0;i<7;i++)prop(root,hoop,i%3?cyan:pink,x,y,-9-i*7,1,1,1);
  for(int j=0;j<8;j++){float a=j*6.2831853f/8;lg_stick(root,cyl,cyan,(t3_vec3){x+cosf(a)*r,y+sinf(a)*r,-9},(t3_vec3){x+cosf(a)*r,y+sinf(a)*r,-51},.075f);}
  t3_release(hoop);
 }
 /* Luminous traffic/data bands flow through the middle distance, not the road. */
 for(int i=0;i<8;i++){t3_object *packet=t3_group_new();float x=100+i*150;packet->position=(t3_vec3){x,ground_at(x)+5+(i%3)*6,-60};prop(packet,box,i%2?cyan:pink,0,0,0,18,.10f,.13f);prop(packet,box,cyan,-10,0,0,2,.16f,.16f);gg_animate(parent,packet,4);t3_release(packet);}
}
static void lightgrid_build(cw_node parent,t3_object *root,const track_desc *t) {
 if(strcmp(t->theme,"lightgrid"))return;
 t3_geometry *box=t3_box_geometry_new(1,1,1,1,1,1),*cyl=t3_cylinder_geometry_new(1,1,1,8,1,false);
 t3_material *black=circuit_glass(.18f),*cyan=stunt_color(.01f,.55f,1,4),*pink=stunt_color(.85f,.015f,.25f,3),*white=stunt_color(.45f,.8f,1,2);
 black->env_map_intensity=.5f;
 lightgrid_horizon(parent,root,t,box,cyl,black,cyan,pink);
 /* Thin luminous lines define a huge black volume without filling it with fog. */
 for(float x=-50;x<t->length+100;x+=30) {
  float y=ground_at(x)-8;prop(root,box,cyan,x,y,-65,.035f,.035f,120);
  for(int k=0;k<2;k++){float z=-35-k*60,h=8+22*rnd();prop(root,box,black,x,y+h*.5f,z,5,h,5);t3_material *glow=(int)(x/12)%4?cyan:pink;for(int side=-1;side<=1;side+=2){prop(root,box,glow,x+side*2.52f,y+h*.5f,z+2.52f,.045f,h,.045f);prop(root,box,glow,x,y+h,z+side*2.52f,5.1f,.055f,.055f);}}
 }
 /* Edge strips conform to the road. Black gaps stay readable against their rims. */
 mb edges={0};for(int p=0;p<t->piece_count;p++){const track_piece *pc=&t->pieces[p];for(int j=1;j<pc->count;j++)for(int side=-1;side<=1;side+=2){float x0=pc->pts[(j-1)*2],x1=pc->pts[j*2],y0=pc->pts[(j-1)*2+1],y1=pc->pts[j*2+1],z=side*(TRACK_HALF_W-.025f);int a=edges.nv;mb_v(&edges,(t3_vec3){x0,y0+.025f,z-.055f},(t3_vec3){0,1,0},0,0,1);mb_v(&edges,(t3_vec3){x0,y0+.025f,z+.055f},(t3_vec3){0,1,0},0,1,1);mb_v(&edges,(t3_vec3){x1,y1+.025f,z+.055f},(t3_vec3){0,1,0},1,1,1);mb_v(&edges,(t3_vec3){x1,y1+.025f,z-.055f},(t3_vec3){0,1,0},1,0,1);mb_t(&edges,a,a+1,a+2);mb_t(&edges,a,a+2,a+3);}}
 t3_geometry *eg=mb_done(&edges);add(root,eg,cyan,false);t3_release(eg);
 /* Receding portal frames, deliberately open across the riding line. */
 for(int i=0;i<15;i++) {float x=95+i*95,y=track_height_at(x,NULL);t3_material *m=i%3?cyan:pink;
  for(int side=-1;side<=1;side+=2){prop(root,box,black,x,y+6,side*5.2f,.8f,12,.8f);prop(root,box,m,x+.43f,y+6,side*5.2f,.06f,12,.12f);}
  prop(root,box,black,x,y+12,0,.8f,.8f,11.2f);prop(root,box,m,x+.43f,y+12,0,.06f,.12f,11.2f);
 }
 /* Slowly rotating reactor hoops punctuate the skyline, away from collision. */
 t3_geometry *ring=t3_torus_geometry_new(15,.12f,8,96,6.2831853f);
 for(int i=0;i<4;i++){float x=260+i*350;t3_object *r=t3_group_new();r->position=(t3_vec3){x,ground_at(x)+17,-35};prop(r,ring,i%2?pink:cyan,0,0,0,1,1,1);prop(r,ring,white,0,0,-.3f,.7f,.7f,.7f);for(int j=0;j<6;j++){float a=j*6.2831853f/6;lg_stick(r,cyl,cyan,(t3_vec3){cosf(a)*10.5f,sinf(a)*10.5f,0},(t3_vec3){cosf(a)*15,sinf(a)*15,0},.08f);}gg_animate(parent,r,3);t3_release(r);}
 t3_release(ring);t3_release(box);t3_release(cyl);t3_release(black);t3_release(cyan);t3_release(pink);t3_release(white);
}
