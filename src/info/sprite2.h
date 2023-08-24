// Make sure to be conscious of FF_FRAMEMASK and the fact sprite2 is stored as a uint8_t whenever you change this table.
// Currently, FF_FRAMEMASK is 0xff, or 255 - but the second half is used by FF_SPR2SUPER, so the limitation is 0x7f.
// Since this is zero-based, there can be at most 128 different SPR2_'s without changing that.

_(STIN) _(STIL) _(STIR) // Still
_(STGL) _(STGR) // Still (glance back)
_(STLL) _(STLR) // Still (look back)

_(SLWN) _(SLWL) _(SLWR) // Slow driving
_(SLGL) _(SLGR) // Slow (glance back)
_(SLLL) _(SLLR) // Slow (look back)

_(FSTN) _(FSTL) _(FSTR) // Fast driving
_(FSGL) _(FSGR) // Fast (glance back)
_(FSLL) _(FSLR) // Fast (look back)

_(DRLN) _(DRLO) _(DRLI) // Drifting left
_(DRRN) _(DRRO) _(DRRI) // Drifting right

_(SPIN) // Spinout
_(DEAD) // Dead

_(SIGN) // Finish signpost
_(XTRA) // Three Faces of Darkness
_(TALK) // Talking

_(KART) // Extra frames from Kart skins
