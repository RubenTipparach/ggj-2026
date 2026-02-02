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
 * MOM DIALOG - Day-specific instructions
 *============================================================================*/

/* Day 1: Simple delivery instructions (2 lines) */
#define MOM_DIALOG_DAY1_0 \
	"Good morning dear! I have prepared some food\n" \
	"for our neighbors. Can you please deliver\n" \
	"them for me?"

#define MOM_DIALOG_DAY1_1 \
	"Don't be late for supper!"

#define MOM_DIALOG_DAY1_COUNT 2

/* Day 2: Growing concern (1 line) */
#define MOM_DIALOG_DAY2_0 \
	"Another delivery today, sweetie.\n" \
	"Be extra careful out there,\n" \
	"come straight home when you're done."

#define MOM_DIALOG_DAY2_COUNT 1

/* Day 3: Struggling (1 line) */
#define MOM_DIALOG_DAY3_0 \
	"We're barely hanging on hun.\n" \
	"I really need you to help out\n" \
	"the shop today."

#define MOM_DIALOG_DAY3_COUNT 1

/* Day 4: Protective (1 line) */
#define MOM_DIALOG_DAY4_0 \
	"I don't want you going out today.\n" \
	"I'm just waiting for the substitute\n" \
	"driver for Mr. Bito."

#define MOM_DIALOG_DAY4_COUNT 1

/* Day 5: Mom is GONE - no note */
#define MOM_FAREWELL_NOTE \
	"Mom isn't here...\n\n" \
	"There's a food box on the floor.\n" \
	"One last delivery to make."

static const char *MOM_MASK_DIALOG =
	"Oh by the way, if you can hand out some\n"
	"magical masks, that would help our people\n"
	"hide among the crowds and walk freely in\n"
	"daylight without fear of being taken.";

static const char *MOM_MORE_FOOD_DIALOG =
	"Good job delivering that mask!\n"
	"Here's more food for the next house.";

/*============================================================================
 * MOM COMMENTARY - Day-specific small talk (multiple lines per day)
 *============================================================================*/

/* Day 1: Optimistic (3 lines) */
#define MOM_COMMENT_DAY1_0 \
	"Did you hear the enforcers are in town?\n" \
	"Apparently they're here to help the\n" \
	"police clean up the streets."

#define MOM_COMMENT_DAY1_1 \
	"Business has been slow.\n" \
	"People are staying home more."

#define MOM_COMMENT_DAY1_2 \
	"Try not to worry, sweetie.\n" \
	"Everything will be back to normal soon."

#define MOM_COMMENT_DAY1_COUNT 3

/* Day 2: Growing concern (3 lines) */
#define MOM_COMMENT_DAY2_0 \
	"Your father called. He talked to the\n" \
	"chief of police. The chief said he's\n" \
	"got a guy making sure they don't\n" \
	"arrest kids."

#define MOM_COMMENT_DAY2_1 \
	"Some of the delivery drivers never\n" \
	"showed up for their shifts today.\n" \
	"I hope they're ok."

#define MOM_COMMENT_DAY2_2 \
	"Promise me you'll come straight\n" \
	"home, okay?"

#define MOM_COMMENT_DAY2_COUNT 3

/* Day 3: Frightened (2 lines) */
#define MOM_COMMENT_DAY3_0 \
	"Protestors gathered outside this\n" \
	"morning. The enforcers tear gassed\n" \
	"them all. They were protesting\n" \
	"peacefully!"

#define MOM_COMMENT_DAY3_1 \
	"Please be careful.\n" \
	"You're all I have left."

#define MOM_COMMENT_DAY3_COUNT 2

/* Day 4: Terrified (3 lines) */
#define MOM_COMMENT_DAY4_0 \
	"I heard enforcers kicking down the\n" \
	"neighbor's door last night.\n" \
	"I couldn't sleep after."

#define MOM_COMMENT_DAY4_1 \
	"They're going door to door. If they\n" \
	"knock here, run and hide in the\n" \
	"closet. Don't answer the door for\n" \
	"any strangers."

#define MOM_COMMENT_DAY4_2 \
	"I love you. I need you to know that.\n" \
	"Whatever happens."

#define MOM_COMMENT_DAY4_COUNT 3

/*============================================================================
 * GAME ENDING
 *============================================================================*/

static const char *ENDING_TEXT_1 =
	"A few Elmorians saved.\n"
	"It wasn't enough. It's never enough.\n\n"
	"Your neighbors got together to hire a\n"
	"lawyer to track down your parents.\n\n"
	"The ship leaves tomorrow with a rag tag\n"
	"crew to go bring them home.\n\n"
	"You join your uncle to embark on\n"
	"the adventure.\n\n"
	"But word has spread of your courage.\n"
	"Your actions rallied the people,\n"
	"and gave them hope.";

static const char *ENDING_TEXT_2 =
	"The Galactic Council has paused the\n"
	"deportations while they assess the\n"
	"Empire's enforcement methods.\n\n"
	"At the capitol square,\n"
	"hundreds of thousands stand in\n"
	"silent solidarity. Watching. Waiting.\n\n"
	"But for now - for this moment -\n"
	"they are not alone.\n\n"
	"And sometimes, that's all that matters.";

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
