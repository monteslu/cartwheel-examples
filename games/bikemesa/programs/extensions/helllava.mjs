// Project-owned three.c surface: flowing incandescent fissures, cool basalt,
// image-derived micro-normal, heat-dependent roughness, standard scene lighting.
export const name='helllava';
export async function apply({TSL,THREE,mat,props,sentinel}) {
 const {uniform,texture,uv,vec2,vec3,vec4,float,sin,cos,mix,smoothstep,max,normalize,cameraViewMatrix,normalWorldGeometry,materialColor,materialEmissive,materialRoughness,positionLocal}=TSL;
 const values=[sentinel(),sentinel(),sentinel(),sentinel()];
 const clock=uniform(new THREE.Vector4(...values)).setName('hellClock');
 props['ext.hellClock']={kind:'vec4',value:values};
 const map=new THREE.DataTexture(new Uint8Array(16).fill(128),2,2);
 map.name='ext.hellSurface';map.wrapS=map.wrapT=THREE.RepeatWrapping;
 map.minFilter=THREE.LinearMipmapLinearFilter;map.magFilter=THREE.LinearFilter;
 map.generateMipmaps=true;map.colorSpace=THREE.SRGBColorSpace;map.needsUpdate=true;
 // Real vertex displacement: slow viscous swells, not a flat scrolling sheet.
 const x=positionLocal.x,z=positionLocal.z,t=clock.x;
 const a=x.mul(.83).add(sin(z.mul(.6).add(t.mul(.22))).mul(.6)).sub(t.mul(.48));
 const b=x.mul(.38).add(z.mul(.54)).add(t.mul(.31));
 const d=z.mul(1.7).sub(x.mul(.25)).sub(t.mul(.65));
 const height=sin(a).mul(.10).add(sin(b).mul(.08)).add(sin(d).mul(.035));
 mat.positionNode=vec3(x,positionLocal.y.add(height),z);
 const dx=cos(a).mul(.083).add(cos(b).mul(.0304)).sub(cos(d).mul(.00875));
 const dz=cos(a).mul(cos(z.mul(.6).add(t.mul(.22)))).mul(.036).add(cos(b).mul(.0432)).add(cos(d).mul(.0595));
 const u=uv().add(vec2(clock.x.mul(.014),clock.x.mul(.006)));
 const warp=vec2(sin(u.y.mul(17).add(clock.x.mul(.14))),cos(u.x.mul(13).sub(clock.x.mul(.11)))).mul(.025);
 // Keep samples vec4 until use: chained .rgb.r emits xyz.x and triggers
 // a Tint/Metal swizzle-lowering failure on the tested M2 WebGPU runtime.
 const p=u.add(warp),c=texture(map,p);
 const heat=smoothstep(.015,.24,c.r.sub(c.b));
 const cx=texture(map,p.add(vec2(.001,0))),cy=texture(map,p.add(vec2(0,.001)));
 const h=c.r.add(c.g).add(c.b),hx=cx.r.add(cx.g).add(cx.b),hy=cy.r.add(cy.g).add(cy.b);
 const n=normalize(normalWorldGeometry.add(vec3(h.sub(hx).mul(1.4).sub(dx),float(0),h.sub(hy).mul(1.4).sub(dz))));
 mat.normalNode=normalize(cameraViewMatrix.mul(vec4(n,0)).xyz);
 mat.colorNode=vec4(c.rgb.mul(mix(float(.85),float(.14),heat)).mul(materialColor.rgb),1);
 mat.roughnessNode=mix(float(.78),float(.24),heat).mul(materialRoughness);
 const pulse=float(1).add(sin(clock.x.mul(.8).add(u.x.mul(5)).add(u.y.mul(8))).mul(.10));
 // A temperature ramp keeps molten seams orange/yellow instead of the
 // washed-out photograph's pale highlights. Cool crust remains nonemissive.
 const temperature=mix(vec3(1,.055,.001),vec3(1,.52,.025),smoothstep(.12,.8,c.r));
 mat.emissiveNode=temperature.mul(heat.pow(1.4)).mul(4.5).mul(pulse).mul(materialEmissive);
}
