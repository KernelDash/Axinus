#include <fcntl.h>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
  (void)sig;
  running = 0;
}

struct key {
  int code;
  bool down;
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Not enough arguments.\nUsage: %s EVENT_NUMBER\nYou can find the "
           "event number of your device by using tools such as evtest.\n",
           argv[0]);
    return 1;
  }

  char event_string[32];
  snprintf(event_string, sizeof(event_string), "/dev/input/event%s", argv[1]);

  struct key keys[] = {{KEY_A, 0}, {KEY_D, 0}};
  const size_t key_count = sizeof(keys) / sizeof(keys[0]);
  struct libevdev *dev = NULL;
  struct libevdev_uinput *uidev = NULL;

  int fd = open(event_string, O_RDONLY);
  if (fd < 0) {
    perror("Failed to open event");
    return 1;
  }
  printf("Opened event for reading.\n");

  int rc = libevdev_new_from_fd(fd, &dev);
  if (rc < 0) {
    fprintf(stderr, "Failed to init libevdev: %d\n", rc);
    close(fd);
    return 1;
  }
  printf("Initialized libevdev.\n");

  printf("Device: %s\n", libevdev_get_name(dev));

  rc = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED,
                                          &uidev);
  if (rc < 0) {
    fprintf(stderr, "Failed to create uinput device: %d\n", rc);
    libevdev_free(dev);
    close(fd);
    return 1;
  }
  printf("Created uinput device.\n");

  if (libevdev_get_event_value(dev, EV_KEY, KEY_ENTER)) {
    printf("Waiting for enter to be released...\n");
    struct input_event sync_ev;

    while (libevdev_get_event_value(dev, EV_KEY, KEY_ENTER)) {
      rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &sync_ev);
      if (rc < 0) {
        break;
      }
    }
  }

  if (libevdev_grab(dev, LIBEVDEV_GRAB) < 0) {
    perror("Failed to grab device");
    return 1;
  }
  printf("Grabbed device.\n");

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);
  while (running) {
    struct input_event ev;

    rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
    if (rc != 0)
      continue;

    if (ev.type != EV_KEY) {
      libevdev_uinput_write_event(uidev, ev.type, ev.code, ev.value);
      continue;
    }
    if (ev.value == 2) {
      libevdev_uinput_write_event(uidev, ev.type, ev.code, ev.value);
      continue;
    }

    int target_idx = -1;

    for (size_t i = 0; i < key_count; i++) {
      if (ev.code == keys[i].code) {
        target_idx = (int)i;
        break;
      }
    }

    if (target_idx != -1) {
      int other_idx = 1 - target_idx; // Works only for 2 keys

      if (ev.value == 1) { // On press
        keys[target_idx].down = true;
        if (keys[other_idx].down) {
          libevdev_uinput_write_event(uidev, ev.type, keys[other_idx].code, 0);
          libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
        }

      } else if (ev.value == 0) { // On release
        keys[target_idx].down = false;

        if (keys[other_idx].down) {
          libevdev_uinput_write_event(uidev, ev.type, keys[other_idx].code, 1);
          libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
        }
      }
    }
    libevdev_uinput_write_event(uidev, ev.type, ev.code, ev.value);
  }

  printf("\nCleaning up.\n");
  libevdev_grab(dev, LIBEVDEV_UNGRAB);
  libevdev_uinput_destroy(uidev);
  libevdev_free(dev);
  close(fd);
  return 0;
}
