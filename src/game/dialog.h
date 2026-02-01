/*
 * Dialog and text content for the game
 * All dialog box text, intro text, and story content
 */

#pragma once

/*============================================================================
 * INTRO SEQUENCE TEXT
 *============================================================================*/

static const char *INTRO_QUOTE =
"\"I'm sending someone I trust,\n"
"someone I know I can trust.\n"
"You'll ask if he's alone.\n"
"He will say, 'I have friends\n"
"everywhere...'\"\n"
"\n"
"                  --Luthen Rael, from Andor";

static const char *INTRO_STORY =
"Elmoria has fallen. The Empire is\n"
"in freefall as stock markets\n"
"plummet. The surviving refugees\n"
"settle on Mencora, a planet in a\n"
"neighboring star system.\n"
"\n"
"The Mencorans, however, have also\n"
"suffered economically due to\n"
"mismanagement at the hands of the\n"
"Empire.\n"
"\n"
"Seeking someone to blame,\n"
"the Empire have decided to deport the\n"
"last of the Elmorians offworld,\n"
"to impoverished penal colonies\n"
"in deep space.";

/*============================================================================
 * MOM DIALOG
 *============================================================================*/

/* Dialog strings - ~50 chars per line max */
static const char *MOM_DIALOG =
	"Good morning dear! I have prepared some food\n"
	"for our neighbors. Can you please deliver\n"
	"them for me?";

static const char *MOM_MASK_DIALOG =
	"Oh by the way, if you can hand out some\n"
	"magical masks, that would help our people\n"
	"hide among the crowds and walk freely in\n"
	"daylight without fear of being taken.\n"
	"Just make sure the right people get the\n"
	"right masks.";

static const char *MOM_MORE_FOOD_DIALOG =
	"Good job delivering that mask!\n"
	"Here's more food for the next house.";

/*============================================================================
 * CITIZEN DIALOG
 *============================================================================*/

static const char *CITIZEN_ACCEPT_FOOD =
	"Thank you so much for the food!\n"
	"Here, take this mask. It may help\n"
	"someone who needs to stay hidden.";

static const char *CITIZEN_REJECT_FOOD =
	"Sorry, this food isn't for us.\n"
	"Try one of the other houses.";

static const char *ADULT_ACCEPT_MASK =
	"A mask! Now I can move freely.\n"
	"Thank you, little one.\n\n"
	"Return to mom for more supplies.";

static const char *CITIZEN_GREETING =
	"Hello there, little one!\n"
	"Stay safe out there.";

/*============================================================================
 * PLAYER MESSAGES
 *============================================================================*/

static const char *NEED_TO_TALK_MSG =
	"I should talk to mom about the deliveries first.";

static const char *NEED_FOOD_MSG =
	"I need to pick up the food for delivery.";
