#define _POSIX_SOURCE
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <unistd.h>
#include <sys/types.h>
#include <signal.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <gtk/gtk.h>

typedef struct Data {
	const char* recordings_dir_path;
	GtkWidget* recordings_list_box;
	GtkWidget* right_box;
	size_t index;
	pid_t recording_pid;
	pid_t playing_pid;
	const char* currently_playing_song;
} Data;

static int is_sdb(GVolume* volume) {
	char* dev_path = g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	if (dev_path == NULL) {
		return 0;
	}
	int is_sdb = strstr(dev_path, "/dev/sdb") != NULL;
	free(dev_path);
	return is_sdb;
}

static GFile* get_volume_root(GVolume* volume) {
	GFile* root = NULL;
	if (is_sdb(volume)) {
		GMount* mount = g_volume_get_mount(volume);
		root = g_mount_get_root(mount);
		g_object_unref(mount);
	}
	return root;
}

static void on_play_button_clicked(GtkWidget* play_button, Data* data) {
	const char* full_filename = g_object_get_data(G_OBJECT(play_button), "full_filename");
	GtkWidget* play_image = gtk_button_get_image(GTK_BUTTON(play_button));
	puts(full_filename);
	if (data->recording_pid != 0) {
		// shouldn't even happen
		return;
	}
	if (data->playing_pid != 0) {
		if (strcmp(full_filename, data->currently_playing_song) != 0) {
			// something else is playing. Too lazy to handle right now.
			return;
		}
		// stop
		printf("playing pid: %d\n", data->playing_pid);
		kill(data->playing_pid, SIGTERM);
		gtk_image_set_from_icon_name(GTK_IMAGE(play_image), "media-playback-start", GTK_ICON_SIZE_BUTTON);
		data->playing_pid = 0;
		data->currently_playing_song = NULL;
	} else {
		// start
		data->playing_pid = fork();
		data->currently_playing_song = full_filename;
		if (data->playing_pid == 0) {
			//child
			execl("/usr/bin/ecasound", "/usr/bin/ecasound", "-i", full_filename, "-o", "alsa", (char*) 0);
		}
		gtk_image_set_from_icon_name(GTK_IMAGE(play_image), "media-playback-stop", GTK_ICON_SIZE_BUTTON);
	}
}

static void update_recordings_list(Data* data) {
	GList *children, *iter;
	children = gtk_container_get_children(GTK_CONTAINER(data->recordings_list_box));
	for(iter = children; iter != NULL; iter = g_list_next(iter)) {
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	}
	g_list_free(children);

	if (!g_file_test(data->recordings_dir_path, G_FILE_TEST_IS_DIR)) {
		g_mkdir_with_parents(data->recordings_dir_path, 0666);
	}
	GDir* dir = g_dir_open(data->recordings_dir_path, 0, NULL);
	data->index = 0;
	GtkWidget* inner_box;
	GtkWidget* play_button;

	for (const gchar* filename = g_dir_read_name(dir); filename != NULL; filename = g_dir_read_name(dir)) {
		// only display .mp3 files
		if (strcmp(strrchr(filename, '.'), ".mp3") != 0) {
			continue;
		}
		data->index++;
		inner_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		play_button = gtk_button_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_BUTTON);
		gtk_button_set_relief(GTK_BUTTON(play_button), GTK_RELIEF_NONE);
		char* full_filename = (char*) malloc((strlen(data->recordings_dir_path) + 1 + strlen(filename)) * sizeof(char));
		sprintf(full_filename, "%s/%s", data->recordings_dir_path, filename);
		g_object_set_data(G_OBJECT(play_button), "full_filename", full_filename);
		g_signal_connect(GTK_BUTTON(play_button), "clicked", G_CALLBACK(on_play_button_clicked), data);

		gtk_box_pack_start(GTK_BOX(inner_box), gtk_label_new(filename), TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(inner_box), play_button, FALSE, FALSE, 0);
		gtk_container_add(GTK_CONTAINER(data->recordings_list_box), inner_box);
	}
	g_dir_close(dir);
	gtk_widget_show_all(data->recordings_list_box);
}

static void on_delete_button_clicked(GtkButton* delete_button, Data* data) {
	if (data->index < 1) {
		return;
	}
	GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(data->recordings_list_box), data->index - 1);

	const char* filename = gtk_label_get_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(row))));
	char full_filename[strlen(data->recordings_dir_path) + 1 + strlen(filename)];
	sprintf(full_filename, "%s/%s", data->recordings_dir_path, filename);
	g_unlink(full_filename);
	update_recordings_list(data);
}

static void on_recording_button_toggled(GtkToggleButton* recording_button, Data* data) {
	if (gtk_toggle_button_get_active(recording_button)) {
		gtk_button_set_label(GTK_BUTTON(recording_button), "Aufnahme beenden");
		printf("toggled button, recording path: %s, index %i\n", data->recordings_dir_path, data->index);
		data->recording_pid = fork();
		if (data->recording_pid == 0) {
			// in the child process
			char filename[strlen(data->recordings_dir_path) + 1 + 4 + 4];
			sprintf(filename, "%s/%04d.mp3", data->recordings_dir_path, data->index);
			execl("/usr/bin/ecasound", "/usr/bin/ecasound", "-i", "alsa", "-o", filename, (char*) 0);
			//execl("/bin/bash", "/bin/bash", "-c", command, (char*) 0);
		}
		gtk_widget_set_sensitive(data->right_box, FALSE);
	} else {
		gtk_button_set_label(GTK_BUTTON(recording_button), "Aufnahme starten");
		kill(data->recording_pid, SIGTERM);
		update_recordings_list(data);
		printf("pid: %i\n", data->recording_pid);
		data->recording_pid = 0;
		gtk_widget_set_sensitive(data->right_box, TRUE);
	}
}


static void start_gui(GFile* usb_root) {
	char* usb_root_path = g_file_get_path(usb_root);
	time_t timer = time(NULL);
	struct tm* timeinfo = localtime(&timer);
	char rel_dir_name[20];
	strftime(rel_dir_name, 20, "Aufnahme-%F", timeinfo);
	char* recordings_dir_path = (char*) malloc((strlen(usb_root_path) + 21) * sizeof(char));
	strcpy(recordings_dir_path, usb_root_path);
	strcat(recordings_dir_path, "/");
	strcat(recordings_dir_path, rel_dir_name);
	g_free(usb_root_path);

	GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget* h_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget* record_button = gtk_toggle_button_new_with_label("Aufnahme starten");
	GtkWidget* recordings_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recordings_scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	GtkWidget* recordings_frame = gtk_frame_new("Aufnahmen");
	gtk_frame_set_shadow_type(GTK_FRAME(recordings_frame), GTK_SHADOW_ETCHED_IN);
	gtk_frame_set_label_align(GTK_FRAME(recordings_frame), 0.5, 0.5);
	GtkWidget* recordings_list_box = gtk_list_box_new();
	GtkWidget* v_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* delete_button = gtk_button_new_with_label("letzte Aufnahme löschen");

	Data* data = (Data*) malloc(sizeof(Data));
	data->recordings_dir_path = recordings_dir_path;
	data->recordings_list_box = recordings_list_box;
	data->index = -1;
	data->recording_pid = 0;
	data->playing_pid = 0;
	data->currently_playing_song = NULL;
	update_recordings_list(data);
	g_signal_connect(GTK_TOGGLE_BUTTON(record_button), "toggled", G_CALLBACK(on_recording_button_toggled), data);
	g_signal_connect(GTK_BUTTON(delete_button), "clicked", G_CALLBACK(on_delete_button_clicked), data);

	gtk_box_pack_start(GTK_BOX(h_box), record_button, TRUE, TRUE, 10);
	gtk_box_pack_start(GTK_BOX(h_box), v_box, FALSE, TRUE, 10);
	gtk_container_add(GTK_CONTAINER(recordings_scrolled_window), recordings_frame);
	gtk_box_pack_start(GTK_BOX(v_box), recordings_scrolled_window, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(v_box), delete_button, FALSE, TRUE, 10);
	gtk_container_add(GTK_CONTAINER(recordings_frame), recordings_list_box);
	gtk_container_add(GTK_CONTAINER(window), h_box);
	data->right_box = v_box;

	//gtk_window_set_title(GTK_WINDOW(window), "Christians Aufnehmer");
	//gtk_window_fullscreen(GTK_WINDOW(window));
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	gtk_widget_show_all(window);
}

static void on_mount_removed(const GObject* vm, GMount* mount) {
	puts("Powering off...");
	execl("/usr/bin/systemctl", "/usr/bin/systemctl", "poweroff", (char*) 0);
}

static void on_mount_added(GObject* vm, GMount* mount, GtkWidget* window) {
	GFile* usb_root;
	GVolume* volume = g_mount_get_volume(mount);
	if (is_sdb(volume)) {
		usb_root = g_mount_get_root(mount);
		printf("USB drive plugged in.\n");
	}
	g_object_unref(volume);
	if (usb_root != NULL) {
		g_signal_handlers_disconnect_by_func(vm, G_CALLBACK(on_mount_added), NULL);
		g_signal_connect(vm, "mount-removed", G_CALLBACK(on_mount_removed), NULL);
		gtk_window_close(GTK_WINDOW(window));
		start_gui(usb_root);
	}
}

static void on_volume_added(GObject* vm, GVolume* volume, GtkWidget* window) {
	g_volume_mount(volume, G_MOUNT_MOUNT_NONE, NULL, NULL, NULL, NULL);
}

int main(int argc, char** argv) {
	GVolumeMonitor* vm;
	//State state = {NULL};
	GList* volumes;
	GFile* usb_root = NULL;
	vm = g_volume_monitor_get();
	volumes = g_volume_monitor_get_volumes(vm);
	while (volumes && (usb_root == NULL)) {
		usb_root = get_volume_root(volumes->data);
		if (usb_root != NULL) {
			break;
		}
		volumes = volumes->next;
	}
	g_list_free_full(volumes, g_object_unref);
	gtk_init(&argc, &argv);

	if (usb_root == NULL) {
		GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);
		gtk_container_set_border_width (GTK_CONTAINER (window), 10);
		GtkWidget* label = gtk_label_new("Bitte USB-Stick einstecken!");
		gtk_container_add(GTK_CONTAINER(window), label);
		gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ALWAYS);
		gtk_widget_show_all(window);
		g_signal_connect(vm, "mount-added", G_CALLBACK(on_mount_added), window);
		//g_signal_connect(vm, "volume-added", G_CALLBACK(on_volume_added), window);
	} else {
		g_signal_connect(vm, "mount-removed", G_CALLBACK(on_mount_removed), NULL);
		start_gui(usb_root);
	}

	gtk_main();
	return 0;
}
