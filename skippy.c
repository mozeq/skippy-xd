/* Skippy - Seduces Kids Into Perversion
 *
 * Copyright (C) 2004 Hyriand <hyriand@thegraveyard.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <errno.h>
#include "skippy.h"

static int DIE_NOW = 0;

static int
time_in_millis (void)
{
    struct timeval  tp;

    gettimeofday (&tp, 0);
    return(tp.tv_sec * 1000) + (tp.tv_usec / 1000);
}

static dlist *
update_clients(MainWin *mw, dlist *clients, Bool *touched)
{
	dlist *stack, *iter;
	
	stack = dlist_first(wm_get_stack(mw->dpy));
	iter = clients = dlist_first(clients);
	
	if(touched)
		*touched = False;
	
	/* Terminate clients that are no longer managed */
	while(iter)
	{
		ClientWin *cw = (ClientWin *)iter->data;
		if(! dlist_find_data(stack, (void *)cw->client.window))
		{
			dlist *tmp = iter->next;
			clientwin_destroy((ClientWin *)iter->data, True);
			clients = dlist_remove(iter);
			iter = tmp;
			if(touched)
				*touched = True;
			continue;
		}
		clientwin_update(cw);
		iter = iter->next;
	}
	
	/* Add new clients */
	for(iter = dlist_first(stack); iter; iter = iter->next)
	{
		ClientWin *cw = (ClientWin*)dlist_find(clients, clientwin_cmp_func, iter->data);
		if(! cw && (Window)iter->data != mw->window)
		{
			cw = clientwin_create(mw, (Window)iter->data);
			if(! cw)
				continue;
			clients = dlist_add(clients, cw);
			clientwin_update(cw);
			if(touched)
				*touched = True;
		}
	}
	
	dlist_free(stack);
	
	return clients;
}

static dlist *
do_layout(MainWin *mw, dlist *clients, Window focus, Window leader)
{
	CARD32 desktop = wm_get_current_desktop(mw->dpy);
	unsigned int width, height;
	float factor;
	int xoff, yoff;
	dlist *iter, *tmp;
	
	/* Update the client table, pick the ones we want and sort them */
	clients = update_clients(mw, clients, 0);
	
	if(mw->cod)
		dlist_free(mw->cod);
	
	tmp = dlist_first(dlist_find_all(clients, (dlist_match_func)clientwin_validate_func, &desktop));
	if(leader != None)
	{
		mw->cod = dlist_first(dlist_find_all(tmp, clientwin_check_group_leader_func, (void*)&leader));
		dlist_free(tmp);
	} else
		mw->cod = tmp;
	
	if(! mw->cod)
		return clients;
	
	dlist_sort(mw->cod, clientwin_sort_func, 0);
	
	/* Move the mini windows around */
	layout_run(mw, mw->cod, &width, &height);
	factor = (float)(mw->width - 100) / width;
	if(factor * height > mw->height - 100)
		factor = (float)(mw->height - 100) / height;
	
	xoff = (mw->width - (float)width * factor) / 2;
	yoff = (mw->height - (float)height * factor) / 2;
	mainwin_transform(mw, factor);
	for(iter = mw->cod; iter; iter = iter->next)
		clientwin_move((ClientWin*)iter->data, factor, xoff, yoff);
	
	/* Get the currently focused window and select which mini-window to focus */
	iter = dlist_find(mw->cod, clientwin_cmp_func, (void *)focus);
	if(! iter)
		iter = mw->cod;
	mw->focus = (ClientWin*)iter->data;
	mw->focus->focused = 1;
	
	/* Map the client windows */
	for(iter = mw->cod; iter; iter = iter->next)
		clientwin_map((ClientWin*)iter->data);
	XWarpPointer(mw->dpy, None, mw->focus->mini.window, 0, 0, 0, 0, mw->focus->mini.width / 2, mw->focus->mini.height / 2);
	
	return clients;
}

static dlist *
skippy_run(MainWin *mw, dlist *clients, Window focus, Window leader, Bool all_xin)
{
	XEvent ev;
	int die = 0;
	Bool refocus = False;
	int last_rendered;
	
	/* Update the main window's geometry (and Xinerama info if applicable) */
	mainwin_update(mw);
#ifdef XINERAMA
	if(all_xin)
	{
		mw->xin_active = 0;
	}
#endif /* XINERAMA */
	
	/* Map the main window and run our event loop */
	if(mw->lazy_trans)
	{
		mainwin_map(mw);
		XFlush(mw->dpy);
	}
	
	clients = do_layout(mw, clients, focus, leader);
	if(! mw->cod)
		return clients;
	
	/* Map the main window and run our event loop */
	if(! mw->lazy_trans)
		mainwin_map(mw);
	
	last_rendered = time_in_millis();
	while(! die) {
	    int i, j, now, timeout;
	    int move_x = -1, move_y = -1;
	    struct pollfd r_fd;
	    
	    XFlush(mw->dpy);
	    
	    r_fd.fd = ConnectionNumber(mw->dpy);
	    r_fd.events = POLLIN;
	    if(mw->poll_time > 0)
	    	timeout = MAX(0, mw->poll_time + last_rendered - time_in_millis());
	    else
	    	timeout = -1;
	    i = poll(&r_fd, 1, timeout);
	    
	    now = time_in_millis();
	    if(now >= last_rendered + mw->poll_time)
	    {
	    	REDUCE(if( ((ClientWin*)iter->data)->damaged ) clientwin_repair(iter->data), mw->cod);
	    	last_rendered = now;
	    }
	    
	    i = XPending(mw->dpy);
	    for(j = 0; j < i; ++j)
	    {
		XNextEvent(mw->dpy, &ev);
		
		if (ev.type == MotionNotify)
		{
			move_x = ev.xmotion.x_root;
			move_y = ev.xmotion.y_root;
		}
		else if(ev.type == DestroyNotify || ev.type == UnmapNotify) {
			dlist *iter = dlist_find(clients, clientwin_cmp_func, (void *)ev.xany.window);
			if(iter)
			{
				ClientWin *cw = (ClientWin *)iter->data;
				clients = dlist_first(dlist_remove(iter));
				iter = dlist_find(mw->cod, clientwin_cmp_func, (void *)ev.xany.window);
				if(iter)
					mw->cod = dlist_first(dlist_remove(iter));
				clientwin_destroy(cw, True);
				if(! mw->cod)
				{
					die = 1;
					break;
				}
			}
		}
		else if (mw->poll_time >= 0 && ev.type == mw->damage_event_base + XDamageNotify)
		{
			XDamageNotifyEvent *d_ev = (XDamageNotifyEvent *)&ev;
			dlist *iter = dlist_find(mw->cod, clientwin_cmp_func, (void *)d_ev->drawable);
			if(iter)
			{
				if(mw->poll_time == 0)
					clientwin_repair((ClientWin *)iter->data);
				else
					((ClientWin *)iter->data)->damaged = True;
			}
				
		}
		else if(ev.type == KeyRelease && ev.xkey.keycode == mw->key_q)
		{
			DIE_NOW = 1;
			die = 1;
			break;
		}
		else if(ev.type == KeyRelease && ev.xkey.keycode == mw->key_escape)
		{
			refocus = True;
			die = 1;
			break;
		}
		else if(ev.xany.window == mw->window)
			die = mainwin_handle(mw, &ev);
		else if(ev.type == PropertyNotify)
		{
			if(ev.xproperty.atom == ESETROOT_PMAP_ID || ev.xproperty.atom == _XROOTPMAP_ID)
			{
				mainwin_update_background(mw);
				REDUCE(clientwin_render((ClientWin *)iter->data), mw->cod);
			}

		}
		else if(mw->tooltip && ev.xany.window == mw->tooltip->window)
			tooltip_handle(mw->tooltip, &ev);
		else
		{
			dlist *iter;
			for(iter = mw->cod; iter; iter = iter->next)
			{
				ClientWin *cw = (ClientWin *)iter->data;
				if(cw->mini.window == ev.xany.window)
				{
					die = clientwin_handle(cw, &ev);
					if(die)
						break;
				}
			}
			if(die)
				break;
		}
	    
	    }
	    	
	    if(mw->tooltip && move_x != -1)
	    	tooltip_move(mw->tooltip, move_x + 20, move_y + 20);
	    
	}
	
	/* Unmap the main window and clean up */
	mainwin_unmap(mw);
	XFlush(mw->dpy);
	
	REDUCE(clientwin_unmap((ClientWin*)iter->data), mw->cod);
	dlist_free(mw->cod);
	mw->cod = 0;
	
	if(die == 2)
		DIE_NOW = 1;
	
	if(refocus)
	{
		XSetInputFocus(mw->dpy, focus, RevertToPointerRoot, CurrentTime);
	}
	
	return clients;
}

void send_command_to_daemon_via_fifo(int command, const char* pipePath)
{
	FILE *fp;	
	if (access(pipePath, W_OK) != 0)
	{
		fprintf(stderr, "pipe does not exist, exiting...\n");
		exit(1);
	}
	
	fp = fopen(pipePath, "w");
	
	printf("sending command...\n");
	fputc(command, fp);
	
	fclose(fp);
}

void activate_window_picker(const char* pipePath)
{
	send_command_to_daemon_via_fifo(ACTIVATE_WINDOW_PICKER, pipePath);
}

void exit_daemon(const char* pipePath)
{
	send_command_to_daemon_via_fifo(EXIT_RUNNING_DAEMON, pipePath);
}

void show_help()
{
	printf("Usage: skippy-xd [command]\n\n");
	printf("The available commands are:\n");
	printf("\t--start-daemon            - starts the daemon running.\n");
	printf("\t--stop-daemon             - stops the daemon running.\n");
	printf("\t--activate-window-picker  - tells the daemon to show the window picker.\n");
	printf("\t--help                    - show this message.\n\n");
}

int main(int argc, char *argv[])
{
	dlist *clients = 0, *config = 0;
	Display *dpy = XOpenDisplay(NULL);
	MainWin *mw;
	const char *homedir;
	char cfgpath[8192];
	Bool invertShift = False;
	Bool runAsDaemon = False;
	Window leader, focused;
	const char* pipePath;
	int result;
	int flush_file = 0;
	FILE *fp;
	int piped_input;
	int exitDaemon = 0;
	
	homedir = getenv("HOME");
	if(homedir) {
		snprintf(cfgpath, 8191, "%s/%s", homedir, ".config/skippy-xd/skippy-xd.rc");
		config = config_load(cfgpath);
	}
	else
	{
		fprintf(stderr, "WARNING: $HOME not set, not loading config.\n");
	}
	
	pipePath = config_get(config, "general", "pipePath", "/tmp/skippy-xd-fifo");
	
	if (argc > 1)
	{
		if (strcmp(argv[1], "--activate-window-picker") == 0)
		{
			printf("activating window picker...\n");
			activate_window_picker(pipePath);
			exit(0);
		}
		else if (strcmp(argv[1], "--stop-daemon") == 0)
		{
			printf("exiting daemon...\n");
			exit_daemon(pipePath);
			exit(0);
		}
		else if (strcmp(argv[1], "--start-daemon") == 0)
		{
			runAsDaemon = True;
		}
		else if (strcmp(argv[1], "--help") == 0)
		{
			show_help();
			exit(0);
		}	
	}
	
	if(! dpy) {
		fprintf(stderr, "FATAL: Couldn't connect to display.\n");
		return -1;
	}
	
	wm_get_atoms(dpy);
	
	if(! wm_check(dpy)) {
		fprintf(stderr, "FATAL: WM not NETWM or GNOME WM Spec compliant.\n");
		return -1;
	}
	
	wm_use_netwm_fullscreen(strcasecmp("true", config_get(config, "general", "useNETWMFullscreen", "true")) == 0);
	wm_ignore_skip_taskbar(strcasecmp("true", config_get(config, "general", "ignoreSkipTaskbar", "false")) == 0);
	
	mw = mainwin_create(dpy, config);
	if(! mw)
	{
		fprintf(stderr, "FATAL: Couldn't create main window.\n");
		config_free(config);
		XCloseDisplay(mw->dpy);
		return -1;
	}
	
	invertShift = strcasecmp("true", config_get(config, "xinerama", "showAll", "false")) == 0;
	
	XSelectInput(mw->dpy, mw->root, PropertyChangeMask);

	if (runAsDaemon)
	{
		printf("Running as daemon...\n");

		if (access(pipePath, R_OK) == 0)
		{
			printf("access() returned 0\n");
			printf("reading excess data to end...\n");
			flush_file = 1;
		}
		
		result = mkfifo (pipePath, S_IRUSR| S_IWUSR);
		if (result < 0  && errno != EEXIST)
		{
			fprintf(stderr, "Error creating named pipe.\n");
			perror("mkfifo");
			exit(2);
		}
		
		fp = fopen(pipePath, "r");
		
		if (flush_file)
		{
			while (1)
			{
				piped_input = fgetc(fp);
				if (piped_input == EOF)
				{
					break;
				}
			}
			
			fp = fopen(pipePath, "r");
		}
		
		while (!exitDaemon)
		{
			piped_input = fgetc(fp);
			switch (piped_input)
			{
				case ACTIVATE_WINDOW_PICKER:
					leader = None, focused = wm_get_focused(mw->dpy);
					clients = skippy_run(mw, clients, focused, leader, !invertShift);
					break;
				
				case EXIT_RUNNING_DAEMON:
					printf("Exit command received, killing daemon cleanly...\n");
					remove(pipePath);
					exitDaemon = 1;
				
				case EOF:
					#ifdef DEBUG
					printf("EOF reached.\n");
					#endif
					fclose(fp);
					fp = fopen(pipePath, "r");
					break;
				
				default:
					printf("unknown code received: %d\n", piped_input);
					printf("Ignoring...\n");
					break;
			}
		}
	}
	else
	{
		printf("running once then quitting...\n");
		leader = None, focused = wm_get_focused(mw->dpy);
		clients = skippy_run(mw, clients, focused, leader, !invertShift);
	}
	
	dlist_free_with_func(clients, (dlist_free_func)clientwin_destroy);
	
	XFlush(mw->dpy);
	
	mainwin_destroy(mw);
	
	XSync(dpy, True);
	XCloseDisplay(dpy);
	config_free(config);
	
	return 0;
}
