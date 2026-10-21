/* Original Sojourner reconstruction, authored from NASA/JPL reference photos.
 * Local metres; display scale follows the other enlarged discovery props.
 * Static children are flattened for the normal scenery material/chunk batches. */
static void sojourner_link(t3_object *root,t3_geometry *box,t3_material *metal,
                           float ax,float ay,float bx,float by,float z) {
  float dx=bx-ax,dy=by-ay;
  t3_mesh *m=prop(root,box,metal,(ax+bx)*.5f,(ay+by)*.5f,z,sqrtf(dx*dx+dy*dy),.013f,.015f);
  t3_object_set_rotation(&m->base,0,0,atan2f(dy,dx));
}
static void sojourner_landmark(t3_object *root,t3_geometry *box,t3_geometry *cyl,
                               t3_material *metal,t3_material *foil,t3_material *dark,
                               float x,float z,int kind) {
  int first=root->child_count;
  prop(root,box,foil,0,.177f,0,.40f,.14f,.285f);
  prop(root,box,metal,0,.263f,0,.56f,.022f,.40f);
  prop(root,box,dark,0,.278f,0,.59f,.012f,.42f);
  t3_material *solar=t3_mesh_standard_material_new(0x172957);
  solar->metalness=.55f;solar->roughness=.31f;
  mb cells={0};
  for(int row=0;row<8;row++)for(int col=0;col<12;col++) {
    float xx=-.282f+col*.047f,zz=-.193f+row*.048f;
    int v=mb_v(&cells,(t3_vec3){xx,.285f,zz},(t3_vec3){0,1,0},0,0,1);
    mb_v(&cells,(t3_vec3){xx+.043f,.285f,zz},(t3_vec3){0,1,0},1,0,1);
    mb_v(&cells,(t3_vec3){xx+.043f,.285f,zz+.043f},(t3_vec3){0,1,0},1,1,1);
    mb_v(&cells,(t3_vec3){xx,.285f,zz+.043f},(t3_vec3){0,1,0},0,1,1);
    mb_t(&cells,v,v+2,v+1);mb_t(&cells,v,v+3,v+2);
  }
  t3_geometry *grid=mb_done(&cells);add(root,grid,solar,false);t3_release(grid);t3_release(solar);
  for(int side=-1;side<=1;side+=2) {
    float zz=side*.205f;
    sojourner_link(root,box,metal,-.245f,.065f,.025f,.18f,side*.17f);
    sojourner_link(root,box,metal,.025f,.18f,.15f,.125f,side*.17f);
    sojourner_link(root,box,metal,0,.065f,.15f,.125f,side*.17f);
    sojourner_link(root,box,metal,.15f,.125f,.245f,.065f,side*.17f);
    for(int wheel=-1;wheel<=1;wheel++) {
      float xx=wheel*.245f;
      t3_mesh *w=prop(root,cyl,dark,xx,.065f,zz,.065f,.06f,.065f);
      t3_object_set_rotation(&w->base,1.5707963f,0,0);
      w=prop(root,cyl,metal,xx,.065f,zz+side*.031f,.028f,.004f,.028f);
      t3_object_set_rotation(&w->base,1.5707963f,0,0);
      for(int k=0;k<10;k++) {
        float a=k*.62831853f;
        w=prop(root,box,metal,xx+.065f*cosf(a),.065f+.065f*sinf(a),zz,.009f,.007f,.061f);
        t3_object_set_rotation(&w->base,0,0,a);
      }
    }
  }
  /* Low stereo camera pair, rear APXS contact head, thin whip antenna. */
  for(int side=-1;side<=1;side+=2) {
    prop(root,box,metal,.218f,.205f,side*.09f,.045f,.043f,.047f);
    prop(root,box,dark,.243f,.205f,side*.09f,.005f,.025f,.028f);
  }
  sojourner_link(root,box,metal,-.20f,.18f,-.30f,.11f,0);
  prop(root,box,metal,-.315f,.105f,0,.025f,.07f,.07f);
  prop(root,box,dark,-.329f,.105f,0,.004f,.044f,.044f);
  prop(root,cyl,metal,-.20f,.37f,-.14f,.0025f,.19f,.0025f);
  prop(root,cyl,metal,-.20f,.283f,-.14f,.013f,.016f,.013f);
  float scale=3;
  float slope=atan2f(bank_surface_y(x+1,z,kind)-bank_surface_y(x-1,z,kind),2);
  float cross=atan2f(bank_surface_y(x,z+1,kind)-bank_surface_y(x,z-1,kind),2);
  t3_quat q=t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){0,0,1},slope),
    t3_quat_multiply(t3_quat_from_axis_angle((t3_vec3){1,0,0},-cross),t3_quat_from_axis_angle((t3_vec3){0,1,0},-.35f)));
  float y=0;
  for(int side=-1;side<=1;side+=2)for(int wheel=-1;wheel<=1;wheel++) {
    t3_vec3 foot=t3_vec3_apply_quat((t3_vec3){wheel*.245f*scale,0,side*.205f*scale},q);
    y+=bank_surface_y(x+foot.x,z+foot.z,kind)-foot.y;
  }
  y=y/6-.015f;
  for(int i=first;i<root->child_count;i++) {
    t3_object *o=root->children[i];t3_vec3 p=o->position;
    p=t3_vec3_apply_quat((t3_vec3){p.x*scale,p.y*scale,p.z*scale},q);
    o->position=(t3_vec3){x+p.x,y+p.y,z+p.z};
    o->scale=(t3_vec3){o->scale.x*scale,o->scale.y*scale,o->scale.z*scale};
    t3_object_set_quaternion(o,t3_quat_multiply(q,o->quaternion));
  }
}
