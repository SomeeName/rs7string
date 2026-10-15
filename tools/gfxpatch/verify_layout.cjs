/* Execute the JavaScript-compatible AS2 drawing methods with MovieClip stubs.
 * This checks logic and emits a schematic; it is not a Scaleform renderer.
 * Usage: node verify_layout.cjs <gfxpatch-report.json> <preview.svg>
 */
const fs = require('node:fs');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const report = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const methods = ['InitImages','UpdateImages','BuildNutAndFrets','UpdateNutAndFrets',
  'BuildString','UpdateString','InitTextFields','UpdateTextFields','AdjustImagesLayout',
  'ColorFinger','GfxPatchCount','GfxPatchRank','GfxPatchExtraRows','GfxPatchLayout'];

function extract(source, name) {
  const re = new RegExp('function '+name+'\\([^)]*\\)\\s*\\{');
  const m = re.exec(source);
  if (!m) return null;
  let start = m.index, i = start + m[0].length, depth = 1;
  while (depth && i < source.length) {
    if (source[i] === '{') depth++;
    if (source[i] === '}') depth--;
    i++;
  }
  return source.slice(start, i);
}
function clip(asset, init={}) {
  return Object.assign({_x:0,_y:0,_xscale:100,_yscale:100,_alpha:100,_visible:true,
    asset, setTextFormat(){}, gotoAndStop(frame){this.frame=frame;}}, init);
}
function make(source) {
  const context = vm.createContext({
    Color: function(c){this.setRGB = function(rgb){c.rgb=rgb;};},
    TextFormat: function(){},
    rs:{common:{Prefs:{$HEADLINE_FONT:'stub',ORANGE_CLR:0xff8800,WHITE_CLR_HILITE:0xffffff}}}
  });
  const obj = {mImages:{},mTextFields:{},depth:0,
    getNextHighestDepth(){return ++this.depth;},
    CreateImageMovie(asset,name,depth,init){return clip(asset,init);},
    CreateTextField(){return clip('text');},
    attachMovie(asset,name,depth,init){
      const c=clip(asset,init);
      if(asset==='HandDiagram') for(const finger of ['Thumb','Pointer','Middle','Ring','Pinky']) c[finger]=clip(finger);
      return c;
    }
  };
  for(const name of methods) {
    const code=extract(source,name);
    if(code) obj[name]=vm.runInContext('('+code+')',context);
  }
  return obj;
}
const prefixes=['LowE','A','D','G','B','HighE','LowB','LowFs'];
const colours=[0xff0000,0xffff00,0x0000ff,0xff8800,0x00ff00,0x8800ff,0x808080,0x00ffff];
function data(n,left,inverted,frets,scale=100) {
  const d={StartFret:1,Text:'Offline check',Visible:1,LeftHanded:left,Inverted:inverted,xScale:scale,yScale:scale};
  for(let i=0;i<n;i++) {
    d[prefixes[i]+'Fret']=Array.isArray(frets)?frets[i]:frets;
    d[prefixes[i]+'_Finger']=i%5;
    if(i<6) d[prefixes[i]+'_Color']=colours[i]; // extra defaults tested
  }
  return d;
}
function apply(obj,d,init=false) {
  obj.mDataObj=d;
  if(init){obj.InitImages();obj.InitTextFields();}
  else{obj.UpdateImages();obj.UpdateTextFields();}
  obj.AdjustImagesLayout();
}
function snapshot(obj) {
  const fields=['_x','_y','_alpha','_visible','_xscale','_yscale','_height','text','rgb','asset'];
  const pick=c=>Object.fromEntries(fields.filter(k=>c[k]!==undefined).map(k=>[k,c[k]]));
  return JSON.stringify({images:Object.fromEntries(Object.keys(obj.mImages).sort().map(k=>[k,pick(obj.mImages[k])])),
    text:Object.fromEntries(Object.keys(obj.mTextFields).sort().map(k=>[k,pick(obj.mTextFields[k])])),
    bg:pick(obj.mBackground),lesson:pick(obj.mLessonBackground),gradient:pick(obj.mGradient)});
}
function checkRows(obj,n,left,inverted) {
  for(let i=0;i<8;i++) {
    const p=prefixes[i], keys=[p+'StringNoSelected',p+'StringSelected',p+'NoteMuted',p+'NoteOpen'];
    if(i<n) {
      const rank=inverted?(i<6?5-i:i):(i<6?i+(n-6):n-1-i), center=44+34*rank;
      const row=obj.mImages[p+'StringNoSelected'],selected=obj.mImages[p+'StringSelected'];
      assert.equal(row._xscale,obj.mDataObj.xScale||100);
      assert.equal(selected._xscale,obj.mDataObj.xScale||100);
      const fret=obj.mDataObj[p+'Fret'];
      assert.equal(row._alpha,fret===0?0:100);
      assert.equal(selected._alpha,fret===0?100:0);
      assert.equal(obj.mImages[p+'NoteMuted']._alpha,fret<0?100:0);
      assert.equal(obj.mImages[p+'NoteOpen']._alpha,fret===0?100:0);
      if(row._alpha===100) assert.equal(row._y,center-4);
      if(selected._alpha===100) assert.equal(selected._y,center-16);
      const icon=obj.mImages[p+'Note'+['Chord_NoteRed','Chord_NoteYellow','Chord_NoteBlue','Chord_NoteOrange','Chord_NoteGreen','Chord_NotePurple','Chord_NotePurple','Chord_NotePurple'][i]];
      assert.equal(icon._alpha,fret>0?100:0);
      if(icon._alpha===100) {
        assert.equal(icon._y,center-16);
        if(i>=6) assert.equal(icon.rgb,obj.mDataObj[p+'_Color']??colours[i]);
      }
      if(i>=6) {
        const clr=obj.mDataObj[p+'_Color']??colours[i];
        assert.equal((fret===0?selected:row).rgb,clr);
        if(fret<0) assert.equal(obj.mImages[p+'NoteMuted'].rgb,clr);
        if(fret===0) assert.equal(obj.mImages[p+'NoteOpen'].rgb,clr);
      }
    } else {
      for(const key of keys) if(obj.mImages[key]) assert.equal(obj.mImages[key]._alpha,0);
      const icon=obj.mImages[p+'NoteChord_NotePurple'];
      if(icon) assert.equal(icon._alpha,0);
      if(obj.mTextFields[p+'Note']) assert.equal(obj.mTextFields[p+'Note']._visible,false);
    }
  }
  assert.equal(obj.mBackground._height,260+34*(n-6));
  assert.equal(obj.mLessonBackground._height,285+34*(n-6));
  assert.equal(obj.mGradient._height,190+34*(n-6));
  for(let i=1;i<=4;i++) assert.equal(obj.mTextFields['fret'+i]._y,232+34*(n-6));
  const scale=obj.mDataObj.yScale||100;
  assert.equal(obj.mImages.FirstBar._yscale,scale*(192+34*(n-6))/192);
}
let scenarios=0, frames=[];
for(const movie of report.movies) {
  const stock=fs.readFileSync(movie.original_class,'utf8');
  const patched=fs.readFileSync(movie.recompiled_class,'utf8');
  for(const left of [0,1]) for(const inverted of [0,1]) for(const scale of [100,60]) {
    for(const fret of [-1,0,1,2,3,4]) {
      const original=make(stock),extended=make(patched);
      const d=data(6,left,inverted,fret,scale);
      apply(original,d,true);apply(extended,d,true);
      assert.equal(snapshot(extended),snapshot(original),'six-string initialization differs');
      apply(original,d);apply(extended,d);
      assert.equal(snapshot(extended),snapshot(original),'six-string update differs');
      scenarios++;
    }
    const obj=make(patched);
    for(const [step,n] of [6,7,8,6,8,7,6].entries()) {
      const d=data(n,left,inverted,[1,0,-1,2,3,4,1,4],scale);
      apply(obj,d,step===0);
      checkRows(obj,n,left,inverted);
      scenarios++;
    }
    for(const n of [7,8]) for(const fret of [-1,0,1,2,3,4]) {
      const obj=make(patched),d=data(n,left,inverted,fret,scale);
      // Exercise absent fingers and hand illustration without adding fingers.
      delete d.LowB_Finger;delete d.LowFs_Finger;
      d.ShowHand=1; d.Finger0Fret=1; d.Finger0Color=0x808080;
      apply(obj,d,true);apply(obj,d);checkRows(obj,n,left,inverted);
      assert.equal(obj.mTextFields.LowBNote,undefined);
      assert.equal(obj.mHand.frame,left?'Left':'Right');
      assert.equal(obj.mHand.Thumb.rgb,0x808080);
      scenarios++;
    }
  }
  const obj=make(patched),d=data(8,0,0,1);
  d.LowB_Color=0x999999;d.LowFs_Color=0xffffff;
  apply(obj,d,true);
  assert.equal(obj.mImages.LowBNoteChord_NotePurple.rgb,0x999999);
  assert.equal(obj.mImages.LowFsNoteChord_NotePurple.rgb,0xffffff);
  delete d.LowB_Finger;delete d.LowFs_Finger;apply(obj,d);
  assert.equal(obj.mTextFields.LowBNote._visible,false);
  assert.equal(obj.mTextFields.LowFsNote._visible,false);
  // An orphan eighth-string property must not silently add a missing seventh.
  delete d.LowBFret;apply(obj,d);assert.equal(obj.GfxPatchCount(),6);
  checkRows(obj,6,0,0);
  scenarios+=2;
  if(!frames.length) for(const n of [6,7,8]) for(const left of [0,1]) for(const inverted of [0,1]) {
    const obj=make(patched);
    apply(obj,data(n,left,inverted,[1,0,-1,2,3,4,1,4]),true);
    frames.push({obj,n,left,inverted});
  }
}
// Own schematic art: positions are obtained from executed, re-decompiled code.
let svg='<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="1220" viewBox="0 0 1200 1220"><rect width="1200" height="1220" fill="#17191d"/><text x="20" y="25" fill="white" font-family="sans-serif" font-size="18">Offline AS2 layout emulation — schematic markers, not a Scaleform frame</text>';
for(let f=0;f<frames.length;f++) {
  const {obj,n,left,inverted}=frames[f],x=20+(f%4)*295,y=55+Math.floor(f/4)*385;
  svg+=`<g transform="translate(${x},${y})"><text fill="white" font-family="sans-serif" font-size="16">${n} rows · ${left?'left':'right'} · ${inverted?'inverted':'normal'}</text><g transform="translate(0,30) scale(0.52)"><rect x="-3" y="15" width="505" height="${obj.mBackground._height}" rx="12" fill="#25282e"/>`;
  for(let i=0;i<5;i++) svg+=`<path d="M ${30+108*i} 33 v ${192+34*(n-6)}" stroke="#bbb" stroke-width="${i===(left?4:0)?5:2}"/>`;
  for(let i=0;i<n;i++) {
    const p=prefixes[i],clr='#'+colours[i].toString(16).padStart(6,'0');
    const a=obj.mImages[p+'StringNoSelected'],b=obj.mImages[p+'StringSelected'];
    const row=a._alpha===100?a:b,center=row._y+(a._alpha===100?4:16);
    svg+=`<path d="M 30 ${center} H 462" stroke="${clr}" stroke-width="2"/>`;
    const icons=Object.entries(obj.mImages).filter(([k,c])=>k.startsWith(p+'Note')&&c._alpha===100);
    for(const [key,c] of icons) {
      const cx=c._x+(key.endsWith('Open')||key.endsWith('Muted')?16:32),cy=c._y+16;
      if(key.endsWith('Muted')) svg+=`<text x="${cx}" y="${cy+8}" text-anchor="middle" fill="${clr}" font-size="28">×</text>`;
      else svg+=`<circle cx="${cx}" cy="${cy}" r="14" fill="${key.endsWith('Open')?'none':clr}" stroke="${clr}" stroke-width="3"/>`;
    }
    const tf=obj.mTextFields[p+'Note'];
    if(tf&&tf._visible) svg+=`<text x="${tf._x}" y="${tf._y+20}" fill="white" font-size="20">${tf.text}</text>`;
  }
  for(let i=1;i<=4;i++) {
    const t=obj.mTextFields['fret'+i];
    svg+=`<text x="${t._x}" y="${t._y+24}" fill="#fa3" font-size="24">${t.text}</text>`;
  }
  svg+='</g></g>';
}
svg+='</svg>';
fs.writeFileSync(process.argv[3],svg);
const summary={scenarios,movies:report.movies.length,legacy_snapshot_equivalence:true,transitions:[6,7,8,6,8,7,6],handedness_inversion:true,scales:[100,60],render:'schematic MovieClip emulation; no AVM1/Scaleform execution'};
fs.writeFileSync(process.argv[3]+'.json',JSON.stringify(summary,null,2));
console.log(JSON.stringify(summary));
