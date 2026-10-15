/* scene scripts (included by main2.c) */
#if SCENE == 0 || SCENE == 4
static void scene_init(void) {
}
static void scene_update(uint8_t dt) {
  walker_update(dt);
}
#endif
