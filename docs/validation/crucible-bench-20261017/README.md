# Workbench scene and ledge recovery

The bench uses a full 160×144 native scene with separate MIX and BAG pages, two ingredient bays, a nine-card inventory grid, quantity and selection markers, short action cues and a persistent result to collect. Small header tabs show the active page; SELECT toggles them. During the reaction, the tray gives way to the material pair and the product's name, traits and use. Left/right moves through material types; B removes the second ingredient before the first. Navigation never discards materials. The bag has an explicit discard confirmation and remains separate from physical crafting.

The merge follows Crucible's body/contact/contour/reveal sequence, using EGRESS's canonical item bodies. Eleven recipes have seventeen baked deformation poses each. Only the two 32×32 bodies stream: 512 bytes per changed pose through two alternating buffers, 112,128 bytes of total body/idle payload. Two particles keep the merged pair within the native ten-sprites-per-line limit. The scene borrows 68 bank-1 OBJ tiles, backed up in the unused SRAM scratch range B500–B93F and restored exactly on exit. It does not replace world, terrain or actor textures. Selection retains the current page instead of clearing the entire scene.

Crafting still requires an adjacent physical bench and cleared enemies plus pending waves. A adds ingredients, A combines, then A collects and equips the result. Until collection, both ingredient units remain intact. Invalid recipes consume nothing. START opens the menu; the bag equips with SELECT; SELECT in play uses one finite equipped unit.

Ledges and interior holes cost one quarter-heart at most, return the heir to the last safe ground and stop at the last quarter-heart. Half-heart HP storage gains one fractional bit; the HUD uses native quarter/half/three-quarter heart fills. The fraction is included in the continue checkpoint.

Twenty focused native reports pass on the exact current ROMs: separate pages, all eleven recipes in either order, seventeen complete deformation poses, explicit collection, exact borrowed-VRAM restoration, physical bench locks, inventory navigation and discard confirmation, quarter-heart falls and power-off persistence, elemental use, tutorial, continue journals and consecutive gameplay. The four-seed camera-edge animation stress test passes for both mappers, including one complete gameplay update per display. Falls write a checkpoint only when health changes.

Standard SHA-256: `ab05de7b48168d8ba30451fcd038266ba7a846f237a2481c9125287db6efe92e`. RTC SHA-256: `ca6bdd6a1b186ac1e5982c94923719001cd43f274aea5843f14e2362d98ee42d`. Each ROM uses 1,376,256 bytes of the 1,900,000-byte budget.

Website build, browser and public release verification are pending. This remains the ten-floor development build; the full campaign ledger remains open. The proposed 444-discovery catalogue and craftable pets are not part of these ROMs.
