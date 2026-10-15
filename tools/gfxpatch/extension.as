   // gfxpatch-owned extension. All stock code comes from the user's input.
   function GfxPatchCount()
   {
      if(this.mDataObj.LowBFret == undefined) return 6;
      if(this.mDataObj.LowFsFret == undefined) return 7;
      return 8;
   }
   // Vertical slot (0 = top row) of a native string index. Native 0 = low E, 1..5 = A..high e,
   // 6 = low B, 7 = low F#. Slots 0..5 equal the stock layout when there are six rows.
   function GfxPatchRank(stringNdx, inverted)
   {
      var count = this.GfxPatchCount();
      if(inverted == true)
      {
         // High e on top, low strings at the bottom: ..., A, low E, low B, low F#.
         return stringNdx < 6 ? 5 - stringNdx : stringNdx;
      }
      // Low E on top: low F#, low B, low E, A, ..., high e.
      return stringNdx < 6 ? stringNdx + (count - 6) : count - 1 - stringNdx;
   }
   function GfxPatchExtraRows()
   {
      var count = this.GfxPatchCount();
      for(var index = 6; index < 8; index++)
      {
         var prefix = index == 6 ? "LowB" : "LowFs";
         var stringKey = prefix + "String";
         var noteKey = prefix + "Note";
         var icon = "Chord_NotePurple";
         if(index < count)
         {
            var clr = this.mDataObj[prefix + "_Color"];
            if(clr == undefined) clr = index == 6 ? 8421504 : GFXPATCH_EIGHTH_COLOR;
            if(this.mImages[stringKey + "NoSelected"] == undefined)
            {
               this.BuildString(index,this.mDataObj[prefix + "Fret"],clr,stringKey,noteKey,icon,this.mDataObj[prefix + "_Finger"],this.mDataObj.LeftHanded == 1,this.mDataObj.Inverted == 1);
            }
            else
            {
               this.UpdateString(index,this.mDataObj[prefix + "Fret"],clr,stringKey,noteKey,icon,this.mDataObj[prefix + "_Finger"],this.mDataObj.LeftHanded == 1,this.mDataObj.Inverted == 1);
            }
            // Stock fretted markers have baked-in colours; tint extra ones too.
            var tint = new Color(this.mImages[noteKey + icon]);
            tint.setRGB(clr);
         }
         else
         {
            if(this.mImages[stringKey + "NoSelected"] != undefined)
            {
               this.mImages[stringKey + "NoSelected"]._alpha = 0;
               this.mImages[stringKey + "Selected"]._alpha = 0;
               this.mImages[noteKey + "Muted"]._alpha = 0;
               this.mImages[noteKey + "Open"]._alpha = 0;
               this.mImages[noteKey + icon]._alpha = 0;
            }
            if(this.mTextFields[noteKey] != undefined) this.mTextFields[noteKey]._visible = false;
         }
      }
   }
   function GfxPatchLayout()
   {
      var extra = 34 * (this.GfxPatchCount() - 6);
      this.mBackground._height = 260 + extra;
      this.mLessonBackground._height = 285 + extra;
      this.mGradient._height = 190 + extra;
      // Use absolute scale, never compound on a previous update.
      var scale = !this.mDataObj.yScale ? 100 : this.mDataObj.yScale;
      this.mImages.FirstBar._yscale = scale * (192 + extra) / 192;
      this.mImages.Bar1._yscale = scale * (192 + extra) / 192;
      this.mImages.Bar2._yscale = scale * (192 + extra) / 192;
      this.mImages.Bar3._yscale = scale * (192 + extra) / 192;
      this.mImages.Bar4._yscale = scale * (192 + extra) / 192;
      for(var index = 1; index <= 4; index++)
      {
         if(this.mTextFields["fret" + index] != undefined)
            this.mTextFields["fret" + index]._y = 232 + extra;
      }
   }
