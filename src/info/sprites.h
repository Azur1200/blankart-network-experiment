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
//_(CRAB) // Crushstacean
_(CR2B) // Banpyura
_(CSPR) // Banpyura spring
_(JJAW) // Jet Jaw
_(SNLR) // Snailer
_(VLTR) // BASH
_(PNTY) // Pointy
_(ARCH) // Robo-Hood
_(CBFS) // Castlebot Facestabber
_(STAB) // Castlebot Facestabber spear aura
_(SPSH) // Egg Guard
_(ESHI) // Egg Guard's shield
_(GSNP) // Green Snapper
_(GSNL) // Green Snapper leg
_(GSNH) // Green Snapper head
_(MNUS) // Minus
_(MNUD) // Minus dirt
_(SSHL) // Spring Shell
_(UNID) // Unidus
_(CANA) // Canarivore
_(CANG) // Canarivore gas
_(PYRE) // Pyre Fly
_(PTER) // Pterabyte
_(DRAB) // Dragonbomber

// Generic Boss Items
_(JETF) // Boss jet fumes

// Boss 1 (Greenflower)
_(EGGM) // Boss 1
_(EGLZ) // Boss 1 Junk

// Boss 2 (Techno Hill)
_(EGGN) // Boss 2
_(TANK) // Boss 2 Junk
_(GOOP) // Boss 2 Goop

// Boss 3 (Deep Sea)
_(EGGO) // Boss 3
_(SEBH) // Boss 3 Junk
_(FAKE) // Boss 3 Fakemobile
_(SHCK) // Boss 3 Shockwave

// Boss 4 (Castle Eggman)
_(EGGP)
_(EFIR) // Boss 4 jet flame
_(EGR1) // Boss 4 Spectator Eggrobo

// Boss 5 (Arid Canyon)
_(FANG) // replaces EGGQ
_(BRKN)
_(WHAT)
_(VWRE)
_(PROJ) // projector light
_(FBOM)
_(FSGN)
_(BARX) // bomb explosion (also used by barrel)
_(BARD) // bomb dust (also used by barrel)

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
_(TOKE) // Special Stage Token
_(RFLG) // Red CTF Flag
_(BFLG) // Blue CTF Flag
_(BSPH) // Sphere
_(NCHP) // NiGHTS chip
_(NSTR) // NiGHTS star
_(EMBM) // Emblem
_(CEMG) // Chaos Emeralds
_(SHRD) // Emerald Hunt

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
_(MSTV) // MiSc TV sprites
_(XLTV) // eXtra Large TV sprites

_(TRRI) // Red team:  10 RIngs
_(TBRI) // Blue team: 10 RIngs

_(TVRI) // 10 RIng
_(TVPI) // PIty shield
_(TVAT) // ATtraction shield
_(TVFO) // FOrce shield
_(TVAR) // ARmageddon shield
_(TVWW) // WhirlWind shield
_(TVEL) // ELemental shield
_(TVSS) // Super Sneakers
_(TVIV) // InVincibility
_(TV1U) // 1Up
_(TV1P) // 1uP (textless)
_(TVEG) // EGgman
_(TVMX) // MiXup
_(TVMY) // MYstery
_(TVGV) // GraVity boots
_(TVRC) // ReCycler
_(TV1K) // 1,000 points  (1 K)
_(TVTK) // 10,000 points (Ten K)
_(TVFL) // FLame shield
_(TVBB) // BuBble shield
_(TVZP) // Thunder shield (ZaP)

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

// The letter
_(LETR)

// Greenflower Scenery
_(FWR1)
_(FWR2) // GFZ Sunflower
_(FWR3) // GFZ budding flower
_(FWR4)
_(BUS1) // GFZ Bush w/ berries
_(BUS2) // GFZ Bush w/o berries
_(BUS3) // GFZ Bush w/ BLUE berries
// Trees (both GFZ and misc)
_(TRE1) // GFZ
_(TRE2) // Checker
_(TRE3) // Frozen Hillside
_(TRE4) // Polygon
_(TRE5) // Bush tree
_(TRE6) // Spring tree

// Techno Hill Scenery
_(THZP) // THZ1 Steam Flower
_(FWR5) // THZ1 Spin flower (red)
_(FWR6) // THZ1 Spin flower (yellow)
_(THZT) // Steam Whistle tree/bush
_(ALRM) // THZ2 Alarm

// Deep Sea Scenery
_(GARG) // Deep Sea Gargoyle
_(SEWE) // Deep Sea Seaweed
_(DRIP) // Dripping water
_(CORL) // Coral
_(BCRY) // Blue Crystal
_(KELP) // Kelp
_(ALGA) // Animated algae top
_(ALGB) // Animated algae segment
_(DSTG) // DSZ Stalagmites
_(LIBE) // DSZ Light beam

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
_(SFBR) // Small Firebar
_(BFBR) // Big Firebar
_(BANR) // Banner/pole
_(PINE) // Pine Tree
_(CEZB) // Bush
_(CNDL) // Candle/pricket
_(FLMH) // Flame holder
_(CTRC) // Fire torch
_(CFLG) // Waving flag/segment
_(CSTA) // Crawla statue
_(CBBS) // Facestabber statue
_(CABR) // Brambles

// Arid Canyon Scenery
_(BTBL) // Big tumbleweed
_(STBL) // Small tumbleweed
_(CACT) // Cacti
_(WWSG) // Caution Sign
_(WWS2) // Cacti Sign
_(WWS3) // Sharp Turn Sign
_(OILL) // Oil lamp
_(OILF) // Oil lamp flare
_(BARR) // TNT barrel
_(REMT) // TNT proximity shell
_(TAZD) // Dust devil
_(ADST) // Arid dust
_(MCRT) // Minecart
_(MCSP) // Minecart spark
_(SALD) // Saloon door
_(TRAE) // Train cameo locomotive
_(TRAI) // Train cameo wagon
_(STEA) // Train steam

// Red Volcano Scenery
_(FLME) // Flame jet
_(DFLM) // Blade's flame
_(LFAL) // Lavafall
_(JPLA) // Jungle palm
_(TFLO) // Torch flower
_(WVIN) // Wall vines

// Dark City Scenery

// Egg Rock Scenery

// Christmas Scenery
_(XMS1) // Christmas Pole
_(XMS2) // Candy Cane
_(XMS3) // Snowman
_(XMS4) // Lamppost
_(XMS5) // Hanging Star
_(XMS6) // Mistletoe
_(FHZI) // FHZ Ice
_(ROSY)

// Halloween Scenery
_(PUMK) // Pumpkins
_(HHPL) // Dr Seuss Trees
_(SHRM) // Mushroom
_(HHZM) // Misc

// Azure Temple Scenery
_(BGAR) // ATZ Gargoyles
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
_(GSPR) // Grey Diagonal Spring
_(SSWY) // Yellow Horizontal Spring
_(SSWR) // Red Horizontal Spring
_(SSWB) // Blue Horizontal Spring
_(SSWG) // Grey Horizontal Spring
_(BSTY) // Yellow Booster
_(BSTR) // Red Booster

// Environmental Effects
_(RAIN) // Rain
_(SNO1) // Snowflake
_(SNO2) // Blizzard Snowball
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

_(CORK)
_(LHRT)

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

// Secret badniks and hazards, shhhh
_(PENG)
_(POPH)
_(HIVE)
_(BUMB)
_(BBUZ)
_(FMCE)
_(HMCE)
_(CACO)
_(BAL2)
_(SBOB)
_(SBFL)
_(SBSK)
_(HBAT)

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
_(DTRG) // Drop Target
_(BHOG) // Ballhog
_(BHBM) // Ballhog BOOM
_(SPBM) // Self-Propelled Bomb
_(THNS) // Thunder Shield
_(BUBS) // Bubble Shield (not Bubs)
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
_(ITSN) // Multi-Sneaker
_(ITBA) // Multi-Banana
_(ITJA) // Multi-Jawz
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
_(DPIN)

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

_(GCHA) // follower: generic chao
_(CHEZ) // follower: cheese

_(BSSS) // Stacking effect

_(TRCK)

// First person view sprites; this is a sprite so that it can be replaced by a specialized MD2 draw later
_(VIEW)
