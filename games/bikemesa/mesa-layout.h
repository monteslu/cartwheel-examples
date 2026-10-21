/* User-ordered Downtown placement, in track meters.
 * Reorder storefronts here without editing their facade models in mesa.inc.
 * Worth/Burritoholics move as one adjacent pair (Worth on the left).
 * Photo references and queued venues: landmark-references.json. */
#ifndef BIKEMESA_MESA_LAYOUT_H
#define BIKEMESA_MESA_LAYOUT_H
static const struct {
  float heatsync, oro, chupacabra, que_chevere, phantom_fox;
  float az_distilling, main_burger, organic_bean, bri;
  float espiritu, mangos, margaritas, lost_dutchman;
  float east_valley_paranormal, against_the_grain;
  float nile, level_one, tacos_chiwas, proof_bread, mad_candy;
  float nct, gus, diamond_dons;
  float casa_copal, republica, hopes, pizza_mart, guadalupe;
  float worth_burritoholics_center;
} mesa_layout = {
  .worth_burritoholics_center=19, .bri=52, .nct=82, .gus=112,
  .oro=142, .chupacabra=172, .main_burger=202, .az_distilling=232,
  .phantom_fox=262, .que_chevere=292, .tacos_chiwas=322, .espiritu=382,
  .east_valley_paranormal=414, .against_the_grain=444, .nile=474,
  .proof_bread=352, .heatsync=504, .organic_bean=534, .casa_copal=564,
  .diamond_dons=594, .level_one=624, .mad_candy=654, .mangos=684,
  .republica=716, .margaritas=750, .lost_dutchman=784,
  .hopes=818, .pizza_mart=852, .guadalupe=888,
};
/* Lamp columns belong in gaps between venues, away from sign sightlines.
 * Derive from the same editable layout used by the facade models. */
static int mesa_streetlight_positions(float *out) {
  float centers[]={mesa_layout.worth_burritoholics_center,mesa_layout.bri,
    mesa_layout.nct,mesa_layout.gus,mesa_layout.oro,mesa_layout.chupacabra,
    mesa_layout.main_burger,mesa_layout.az_distilling,mesa_layout.phantom_fox,
    mesa_layout.que_chevere,mesa_layout.tacos_chiwas,mesa_layout.proof_bread,
    mesa_layout.espiritu,mesa_layout.east_valley_paranormal,mesa_layout.against_the_grain,
    mesa_layout.nile,mesa_layout.heatsync,mesa_layout.organic_bean,
    mesa_layout.casa_copal,mesa_layout.diamond_dons,mesa_layout.level_one,
    mesa_layout.mad_candy,mesa_layout.mangos,mesa_layout.republica,
    mesa_layout.margaritas,mesa_layout.lost_dutchman,mesa_layout.hopes,
    mesa_layout.pizza_mart,mesa_layout.guadalupe};
  int n=sizeof centers/sizeof centers[0];
  for(int i=1;i<n;i++){float x=centers[i];int j=i;while(j&&centers[j-1]>x){centers[j]=centers[j-1];j--;}centers[j]=x;}
  int count=0;out[count++]=centers[0]-19;
  for(int i=0;i<n-1;i++)out[count++]=(centers[i]+centers[i+1])*.5f;
  out[count++]=centers[n-1]+21;
  return count;
}
/* Half frontage plus clearance for the 7 m generic shops. */
static bool mesa_frontage_reserved(float x) {
  const float spans[][2]={
    {mesa_layout.heatsync,13},{mesa_layout.oro,13},{mesa_layout.chupacabra,13},
    {mesa_layout.que_chevere,12},{mesa_layout.phantom_fox,15},{mesa_layout.az_distilling,13},
    {mesa_layout.main_burger,13},{mesa_layout.organic_bean,12},{mesa_layout.bri,12},
    {mesa_layout.espiritu,15},{mesa_layout.mangos,13},
    {mesa_layout.margaritas,16},{mesa_layout.lost_dutchman,17},
    {mesa_layout.east_valley_paranormal,15},{mesa_layout.against_the_grain,15},
    {mesa_layout.nile,15},{mesa_layout.level_one,9},{mesa_layout.worth_burritoholics_center,15},
    {mesa_layout.tacos_chiwas,10},{mesa_layout.proof_bread,8},{mesa_layout.mad_candy,11},
    {mesa_layout.nct,11},{mesa_layout.gus,12},{mesa_layout.diamond_dons,13},
    {mesa_layout.casa_copal,13},{mesa_layout.republica,16},
    {mesa_layout.hopes,16},{mesa_layout.pizza_mart,16},{mesa_layout.guadalupe,19},
  };
  for(unsigned i=0;i<sizeof spans/sizeof spans[0];i++)
    if(fabsf(x-spans[i][0])<spans[i][1])return true;
  return false;
}
#endif
