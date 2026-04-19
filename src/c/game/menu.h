#ifndef AB_MENU_H
#define AB_MENU_H

typedef enum { MENU_RESULT_START, MENU_RESULT_QUIT } MenuResult;

/* Run the main menu loop. Returns when the player starts or quits.
 * out_num_players : set to 1 or 2
 * out_share_credits: set to 0 or 1 */
MenuResult menu_run(int *out_num_players, int *out_share_credits);

#endif /* AB_MENU_H */
