// Hey, moron! If you change this table, you can actually forget about sprnames in info.c,
// because it uses this macro below and does not need to be touched if you alter any sprites :^)

_(NULL) // invisible object
_(NONE) // invisible but still rendered
_(UNKN)

_(THOK) // Thok! mobj
_(PLAY)
_(KART)
_(TIRE)

// Enemies
_(POSS) // Crawla (Blue)
_(SPOS) // Crawla (Red)
_(FISH) // SDURF
_(BUZZ) // Buzz (Gold)
_(RBUZ) // Buzz (Red)
_(JETB) // Jetty-Syn Bomber
_(JETG) // Jetty-Syn Gunner
_(CCOM) // Crawla Commander
_(DETN) // Deton
_(SKIM) // Skim mine dropper
_(TRET) // Industrial Turret
_(TURR) // Pop-Up Turret
_(SHRP) // Sharp
_(JJAW) // Jet Jaw
_(SNLR) // Snailer
_(VLTR) // BASH
_(PNTY) // Pointy
_(ARCH) // Robo-Hood
_(CBFS) // Castlebot Facestabber
_(SPSH) // Egg Guard
_(ESHI) // Egg Guard's shield
_(GSNP) // Green Snapper
_(MNUS) // Minus
_(SSHL) // Spring Shell
_(UNID) // Unidus
_(BBUZ) // AquaBuzz, for Azure Temple

// Generic Boss Items
_(JETF) // Boss jet fumes

// Boss 1 (Greenflower)
_(EGGM) // Boss 1
_(EGLZ) // Boss 1 Junk

// Boss 2 (Techno Hill)
_(EGGN) // Boss 2
_(TNKA) // Boss 2 Tank 1
_(TNKB) // Boss 2 Tank 2
_(SPNK) // Boss 2 Spigot
_(GOOP) // Boss 2 Goop

// Boss 3 (Deep Sea)
_(EGGO) // Boss 3
_(PRPL) // Boss 3 Propeller
_(FAKE) // Boss 3 Fakemobile
_(SHCK) // Boss 3 Shockwave

// Boss 4 (Castle Eggman)
_(EGGP)
_(EFIR) // Boss 4 jet flame

// Boss 5 (Arid Canyon)
_(EGGQ)

// Boss 6 (Red Volcano)
_(EGGR)

// Boss 7 (Dark City)
_(BRAK)
_(BGOO) // Goop
_(BMSL)

// Boss 8 (Egg Rock)
_(EGGT)

// Cy-Brak-Demon; uses SPR_BRAK as well, but has some extras
_(RCKT) // Rockets!
_(ELEC) // Electricity!
_(TARG) // Targeting reticules!
_(NPLM) // Big napalm bombs!
_(MNPL) // Mini napalm bombs!

// Metal Sonic
_(METL)
_(MSCF)
_(MSCB)

// Collectible Items
_(RING)
_(TRNG) // Team Rings
_(EMMY) // Special Stage Token
_(TOKE) // Special Stage Token
_(RFLG) // Red CTF Flag
_(BFLG) // Blue CTF Flag
_(BSPH) // Sphere
_(NCHP) // NiGHTS chip
_(NSTR) // NiGHTS star
_(EMBM) // Emblem
_(CEMG) // Chaos Emeralds

// Interactive Objects
_(BBLS) // water bubble source
_(SIGN) // Level end sign
_(SPIK) // Spike Ball
_(SFLM) // Spin fire
_(TFLM) // Spin fire (team)
_(USPK) // Floor spike
_(WSPK) // Wall spike
_(WSPB) // Wall spike base
_(STPT) // Starpost
_(BMNE) // Big floating mine
_(PUMI) // Rollout Rock

// Monitor Boxes
_(SRBX)
_(RRBX)
_(BRBX)
_(SHTV)
_(PINV)
_(YLTV)
_(BLTV) // Force shield
_(BKTV) // Bomb shield TV
_(WHTV) // Jump shield TV
_(GRTV) // Pity shield TV
_(ELTV) // Elemental shield TV
_(EGGB) // Eggman box
_(MIXU) // Player mixing monitor
_(RECY) // Recycler (power mixing) monitor
_(QUES) // Random monitor
_(GBTV) // Gravity boots TV
_(PRUP) // 1up
_(PTTV) // Score TVs

// Monitor Miscellany
_(MTEX) // Exploding monitor

// Projectiles
_(MISL)
_(LASR) // GFZ3 laser
_(LASF) // GFZ3 laser flames
_(TORP) // Torpedo
_(ENRG) // Energy ball
_(MINE) // Skim mine
_(JBUL) // Jetty-Syn Bullet
_(TRLS)
_(CBLL) // Cannonball
_(AROW) // Arrow
_(CFIR) // Colored fire of various sorts

// Greenflower Scenery
_(FWR1)
_(FWR2) // GFZ Sunflower
_(FWR3) // GFZ budding flower
_(FWR4)
_(BUS1) // GFZ Bush w/ berries
_(BUS2) // GFZ Bush w/o berries

// Techno Hill Scenery
_(THZP) // Techno Hill Zone Plant
_(ALRM) // THZ2 Alarm

// Deep Sea Scenery
_(GARG) // Deep Sea Gargoyle
_(SEWE) // Deep Sea Seaweed
_(DRIP) // Dripping water
_(CRL1) // Coral 1
_(CRL2) // Coral 2
_(CRL3) // Coral 3
_(BCRY) // Blue Crystal

// Castle Eggman Scenery
_(CHAN) // CEZ Chain
_(FLAM) // Flame
_(ESTA) // Eggman esta una estatua!
_(SMCH) // Small Mace Chain
_(BMCH) // Big Mace Chain
_(SMCE) // Small Mace
_(BMCE) // Big Mace
_(YSPB) // Yellow spring on a ball
_(RSPB) // Red spring on a ball

// Arid Canyon Scenery
_(BTBL) // Big tumbleweed
_(STBL) // Small tumbleweed
_(CACT) // Cacti

// Red Volcano Scenery
_(FLME) // Flame jet
_(DFLM) // Blade's flame

// Dark City Scenery

// Egg Rock Scenery

// Christmas Scenery
_(XMS1) // Christmas Pole
_(XMS2) // Candy Cane
_(XMS3) // Snowman
_(XMS4) // Lamppost
_(XMS5) // Hanging Star
_(XMS6) // Mistletoe

// Azure Temple Scenery
_(RCRY) // ATZ Red Crystal (Target)
_(CFLM) // Green torch flame

// Botanic Serenity Scenery
_(BSZ1) // Tall flowers
_(BSZ2) // Medium flowers
_(BSZ3) // Small flowers
_(BSZ4) // Tulip
_(BSZ5) // Cluster of Tulips
_(BSZ6) // Bush
_(BSZ7) // Vine
_(BSZ8) // Misc things

// Misc Scenery
_(STLG) // Stalagmites
_(DBAL) // Disco

// Powerup Indicators
_(ARMA) // Armageddon Shield Orb
_(ARMF) // Armageddon Shield Ring, Front
_(ARMB) // Armageddon Shield Ring, Back
_(WIND) // Whirlwind Shield Orb
_(MAGN) // Attract Shield Orb
_(ELEM) // Elemental Shield Orb
_(FORC) // Force Shield Orb
_(PITY) // Pity Shield Orb
_(FIRS) // Flame Shield Orb
//_(BUBS) // Bubble Shield Orb
_(ZAPS) // Thunder Shield Orb
_(IVSP) // invincibility sparkles
_(SSPK) // Super Sonic Spark

_(GOAL) // Special Stage goal (here because lol NiGHTS)

// Flickies
_(FBUB) // Flicky-sized bubble
_(FL01) // Bluebird
_(FL02) // Rabbit
_(FL03) // Chicken
_(FL04) // Seal
_(FL05) // Pig
_(FL06) // Chipmunk
_(FL07) // Penguin
_(FL08) // Fish
_(FL09) // Ram
_(FL10) // Puffin
_(FL11) // Cow
_(FL12) // Rat
_(FL13) // Bear
_(FL14) // Dove
_(FL15) // Cat
_(FL16) // Canary
_(FS01) // Spider
_(FS02) // Bat

_(EMER)

// Springs
_(FANS) // Fan
_(STEM) // Steam riser
_(BMPR) // Bumpers
_(BLON) // Balloons
_(SPRY) // Yellow Vertical Spring
_(SPRR) // Red Vertical Spring
_(SPRB) // Blue Vertical Spring
_(SPRG) // Grey Vertical Spring
_(YSPR) // Yellow Diagonal Spring
_(RSPR) // Red Diagonal Spring
_(BSPR) // Blue Diagonal Spring
_(BSTY) // Yellow Booster
_(BSTR) // Red Booster

// Environmental Effects
_(RAIN) // Rain
_(SNO1) // Snowflake
_(SPLH) // Water Splish
_(LSPL) // Lava Splish
_(SPLA) // Water Splash
_(SMOK)
_(BUBL) // Bubble
_(WZAP)
_(DUST) // Spindash dust
_(FPRT) // Spindash dust (flame)
_(TFOG) // Teleport Fog
_(SEED) // Sonic CD flower seed
_(PRTL) // Particle (for fans, etc.)

// Game Indicators
_(SCOR) // Score logo
_(DRWN) // Drowning Timer
_(FLII) // AI flight indicator
_(LCKN) // Target
_(TTAG) // Tag Sign
_(GFLG) // Got Flag sign
_(FNSF) // Finish flag

// Ring Weapons
_(RRNG) // Red Ring
_(RNGB) // Bounce Ring
_(RNGR) // Rail Ring
_(RNGI) // Infinity Ring
_(RNGA) // Automatic Ring
_(RNGE) // Explosion Ring
_(RNGS) // Scatter Ring
_(RNGG) // Grenade Ring

_(PIKB) // Bounce Ring Pickup
_(PIKR) // Rail Ring Pickup
_(PIKA) // Automatic Ring Pickup
_(PIKE) // Explosion Ring Pickup
_(PIKS) // Scatter Ring Pickup
_(PIKG) // Grenade Ring Pickup

_(TAUT) // Thrown Automatic Ring
_(TGRE) // Thrown Grenade Ring
_(TSCR) // Thrown Scatter Ring

// Mario-specific stuff
_(COIN)
_(CPRK)
_(GOOM)
_(BGOM)
_(FFWR)
_(FBLL)
_(SHLL)
_(PUMA)
_(HAMM)
_(KOOP)
_(BFLM)
_(MAXE)
_(MUS1)
_(MUS2)
_(TOAD)

// NiGHTS Stuff
_(NDRN) // NiGHTS drone
_(NSPK) // NiGHTS sparkle
_(NBMP) // NiGHTS Bumper
_(HOOP) // NiGHTS hoop sprite
_(NSCR) // NiGHTS score sprite
_(NWNG) // NIGHTS Wing
_(NPRU) // Nights Powerups
_(CAPS) // Capsule thingy for NiGHTS
_(IDYA) // Ideya
_(NTPN) // Nightopian
_(SHLP) // Shleep

// Debris
_(SPRK) // Sparkle
_(BOM1) // Robot Explosion
_(BOM2) // Boss Explosion 1
_(BOM3) // Boss Explosion 2
_(BOM4) // Underwater Explosion
_(BMNB) // Mine Explosion

// Crumbly rocks
_(ROIA)
_(ROIB)
_(ROIC)
_(ROID)
_(ROIE)
_(ROIF)
_(ROIG)
_(ROIH)
_(ROII)
_(ROIJ)
_(ROIK)
_(ROIL)
_(ROIM)
_(ROIN)
_(ROIO)
_(ROIP)

// Needed for some krat luas....
_(BBAL)

// Level debris
_(GFZD) // GFZ debris
_(BRIC) // Bricks
_(WDDB) // Wood Debris
_(BRIR) // CEZ3 colored bricks
_(BRIB)
_(BRIY)

// Gravity Well Objects
_(GWLG)
_(GWLR)

// SRB1 Badniks
_(SRBA)
_(SRBB)
_(SRBC)
_(SRBD)
_(SRBE)
_(SRBF)
_(SRBG)
_(SRBH)
_(SRBI)
_(SRBJ)
_(SRBK)
_(SRBL)
_(SRBM)
_(SRBN)
_(SRBO)

// SRB2Kart
_(RNDM) // Random Item Box
_(RPOP) // Random Item Box Pop
_(SGNS) // Signpost sparkle
_(FAST) // Speed boost trail
_(DSHR) // Speed boost dust release
_(BOST) // Sneaker booster flame
_(DRPO) // Drift booster flame
_(BOSM) // Sneaker booster smoke
_(KFRE) // Sneaker fire trail
_(KINV) // Lighter invincibility sparkle trail
_(KINB) // Darker invincibility sparkle trail
_(KINF) // Invincibility flash
_(INVI) // Invincibility speedlines
_(ICAP) // Item capsules

_(WIPD) // Wipeout dust trail
_(DRIF) // Drift Sparks
_(BDRF) // Brake drift sparks

// Kart Items
_(RSHE) // Rocket sneaker
_(FITM) // Eggman Monitor
_(BANA) // Banana Peel
_(ORBN) // Orbinaut
_(JAWZ) // Jawz
_(SSMN) // SS Mine
_(KRBM) // SS Mine BOOM
_(LNDM) // Land Mine
_(BHOG) // Ballhog
_(BHBM) // Ballhog BOOM
_(SPBM) // Self-Propelled Bomb
_(THNS) // Thunder Shield
_(BUBS) // Bubble Shield (not Bubs)
_(BUBC) // Bubble Shield damage
_(BSDB) // Bubble Shield shatter debris
_(BWVE) // Bubble Shield waves
_(FLMS) // Flame Shield
_(FLMD) // Flame Shield dash
_(FLMP) // Flame Shield paper sprites
_(FLML) // Flame Shield speed lines
_(FLMF) // Flame Shield flash
_(SINK) // Kitchen Sink
_(SITR) // Kitchen Sink Trail
_(KBLN) // Battle Mode Bumper
_(BEXC) // Battle Bumper Explosion: Crystal
_(BEXS) // Battle Bumper Explosion: Shell
_(BDEB) // Battle Bumper Explosion: Debris
_(BEXB) // Battle Bumper Explosion: Blast
_(TWBS) // Tripwire Boost
_(TWBT) // Tripwire BLASTER
_(DEZL) // DEZ Laser respawn

// Additional Kart Objects
_(POKE) // Pokey
_(AUDI) // Audience members
_(DECO) // Old 1.0 Kart Decoratives + New misc ones
_(DOOD) // All the old D00Dkart objects
_(SNES) // Sprites for SNES remake maps
_(GBAS) // Sprites for GBA remake maps
_(SPRS) // Sapphire Coast Spring Shell
_(BUZB) // Sapphire Coast Buzz Mk3
_(CHOM) // Sapphire Coast Chomper
_(SACO) // Sapphire Coast Fauna
_(CRAB) // Crystal Abyss mobs
_(BRNG) // Chaotix Big Ring

_(BUMP) // Player/shell bump
_(FLEN) // Shell hit graphics stuff
_(CLAS) // items clash
_(PSHW) // thrown indicator
_(ISTA) // instashield layer A
_(ISTB) // instashield layer B

_(PWCL) // Invinc/grow clash VFX

_(ARRO) // player arrows
_(ITEM) // base item
_(ITMO) // Multi-Orbinaut
_(ITMI) // Invincibility
_(ITMN)
_(WANT)

_(PBOM) // player bomb

_(HIT1) // battle points
_(HIT2) // battle points
_(HIT3) // battle points

_(RETI) // player reticule

_(AIDU)

_(KSPK) // Spark radius for the thunder shield
_(LZI1) // Lightning that falls on the player for thunder shield
_(LZI2) // ditto
_(KLIT) // You have a twisted mind. But this actually is for the diagonal lightning.

_(FZSM) // F-Zero NO CONTEST explosion
_(FZBM)

// Various plants
_(SBUS)

_(MARB) // Marble Zone sprites
_(FUFO) // CD Special Stage UFO (don't ask me why it begins with an F)

_(RUST) // Rusty Rig sprites

_(VAPE) // Volcanic Valley

// Hill Top Zone
_(HTZA)
_(HTZB)

// Ports of gardens
_(SGVA)
_(SGVB)
_(SGVC)
_(PGTR)
_(PGF1)
_(PGF2)
_(PGF3)
_(PGBH)
_(DPLR)

// Midnight Channel stuff:
_(SPTL) // Spotlight
_(ENM1) // Shadows (Roaming and static)
_(GARU) // Wind attack roaming shadows use.
_(MARR) // Mayonaka Arrow

// Mementos stuff:
_(REAP)

_(JITB) // Jack In The Box

// Color Drive stuff:
_(CDMO)
_(CDBU)

// Daytona Speedway
_(PINE)

// Egg Zeppelin
_(PPLR)

// Desert Palace
_(DPPT)

// Aurora Atoll
_(AATR)
_(COCO)

// Barren Badlands
_(BDST)
_(FROG)
_(CBRA)
_(HOLE)
_(BBRA)

// Eerie Grove
_(EGFG)

// SMK ports
_(SMKP)
_(MTYM)
_(THWP)
_(SNOB)
_(ICEB)

// Ezo's maps - many single-use sprites!
_(CNDL)
_(ECND)
_(DOCH)
_(DUCK)
_(GTRE)
_(CHES)
_(CHIM)
_(DRGN)
_(LZMN)
_(PGSS)
_(ZTCH)
_(MKMA)
_(MKMP)
_(RTCH)
_(BOWL)
_(BOWH)
_(BRRL)
_(BRRR)
_(HRSE)
_(TOAH)
_(BFRT)
_(OFRT)
_(RFRT)
_(PFRT)
_(ASPK)
_(HBST)
_(HBSO)
_(HBSF)
_(WBLZ)
_(WBLN)

// Opulence
_(OPUL)
_(TGEM)
_(TCOI)

_(FWRK)
_(MXCL)
_(LENS)
_(GRES)

_(WAYP)
_(EGOO)

_(WTRL) // Water Trail
_(WTRU) // Water Trail Underlay

_(BSSS) // Stacking effect

_(BKAD) // Airdrop Dust

_(TRCK)

// Dash Rings
_(RAIR)

// Sneaker Panels
_(BSTP)
_(BSTS)
_(BSTT)

// New Springs
_(SSWY)
_(SSWR)
_(SSWB)

_(GSPR)
_(SSWG)

// Arrow Bullet
_(AWBT)

// Recovery Spin Skid
_(RCSP)

// Horncode
_(FHRN)

// First person view sprites; this is a sprite so that it can be replaced by a specialized MD2 draw later
_(VIEW)

// New Spring.
_(NSPV)
_(NSPD)
_(NSPH)
