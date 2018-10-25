/**********************************************************************/
/*           This is the podem test pattern generator for atpg        */
/*                                                                    */
/*           Author: Bing-Chen (Benson) Wu                            */
/*           last update : 01/21/2018                                 */
/**********************************************************************/

#include "atpg.h"
#include <set>
#include <unordered_set>

#define CONFLICT 2

/* generates a single pattern for a single fault */
int ATPG::podem(const fptr fault, int& current_backtracks) {
  int i,ncktwire,ncktin;
  wptr wpi; // points to the PI currently being assigned
  forward_list<wptr> decision_tree; // design_tree (a LIFO stack)
  wptr wfault;
  int attempt_num = 0;  // counts the number of pattern generated so far for the given fault

  /* initialize all circuit wires to unknown */
  ncktwire = sort_wlist.size();
  ncktin = cktin.size();
  for (i = 0; i < ncktwire; i++) {
    sort_wlist[i]->value = U;
  }
  no_of_backtracks = 0;
  find_test = false;
  no_test = false;
  
  mark_propagate_tree(fault->node);

  /* Fig 7 starts here */
  /* set the initial goal, assign the first PI.  Fig 7.P1 */
  switch (set_uniquely_implied_value(fault)) {
    case TRUE: // if a  PI is assigned 
      sim();  // Fig 7.3
      wfault = fault_evaluate(fault);
      if (wfault != nullptr) forward_imply(wfault);// propagate fault effect
      if (check_test()) find_test = true; // if fault effect reaches PO, done. Fig 7.10
      break;
    case CONFLICT:
      no_test = true; // cannot achieve initial objective, no test
      break;
    case FALSE: 
      break;  //if no PI is reached, keep on backtracing. Fig 7.A 
  }

  /* loop in Fig 7.ABC 
   * quit the loop when either one of the three conditions is met: 
   * 1. number of backtracks is equal to or larger than limit
   * 2. no_test
   * 3. already find a test pattern AND no_of_patterns meets required total_attempt_num */
  while ((no_of_backtracks < backtrack_limit) && !no_test &&
    !(find_test && (attempt_num == total_attempt_num))) {
    
    /* check if test possible.   Fig. 7.1 */
    if (wpi = test_possible(fault)) {
      wpi->flag |= CHANGED;
      /* insert a new PI into decision_tree */
      decision_tree.push_front(wpi);
    }
    else { // no test possible using this assignment, backtrack. 

      while (!decision_tree.empty() && (wpi == nullptr)) {
        /* if both 01 already tried, backtrack. Fig.7.7 */
        if (decision_tree.front()->flag & ALL_ASSIGNED) {
          decision_tree.front()->flag &= ~ALL_ASSIGNED;  // clear the ALL_ASSIGNED flag
          decision_tree.front()->value = U; // do not assign 0 or 1
          decision_tree.front()->flag |= CHANGED; // this PI has been changed
          /* remove this PI in decision tree.  see dashed nodes in Fig 6 */
          decision_tree.pop_front();
        }  
        /* else, flip last decision, flag ALL_ASSIGNED. Fig. 7.8 */
        else {
          decision_tree.front()->value = decision_tree.front()->value ^ 1; // flip last decision
          decision_tree.front()->flag |= CHANGED; // this PI has been changed
          decision_tree.front()->flag |= ALL_ASSIGNED;
          no_of_backtracks++;
          wpi = decision_tree.front(); 
        }
      } // while decision tree && ! wpi
      if (wpi == nullptr) no_test = true; //decision tree empty,  Fig 7.9
    } // no test possible

/* this again loop is to generate multiple patterns for a single fault 
 * this part is NOT in the original PODEM paper  */
again:  if (wpi) {
      sim();
      if (wfault = fault_evaluate(fault)) forward_imply(wfault);
      if (check_test()) {
        find_test = true;
        /* if multiple patterns per fault, print out every test cube */
        if (total_attempt_num > 1) {
          if (attempt_num == 0) {
            display_fault(fault);
          }
          display_io(); 
        }
        attempt_num++; // increase pattern count for this fault

		    /* keep trying more PI assignments if we want multiple patterns per fault
		     * this is not in the original PODEM paper*/
        if (total_attempt_num > attempt_num) {
          wpi = nullptr;
          while (!decision_tree.empty() && (wpi == nullptr)) {
            /* backtrack */
            if (decision_tree.front()->flag & ALL_ASSIGNED) {
              decision_tree.front()->flag &= ~ALL_ASSIGNED;
              decision_tree.front()->value = U;
              decision_tree.front()->flag |= CHANGED;
              decision_tree.pop_front();
            }
            /* flip last decision */
            else {
              decision_tree.front()->value = decision_tree.front()->value ^ 1;
              decision_tree.front()->flag |= CHANGED;
              decision_tree.front()->flag |= ALL_ASSIGNED;
              no_of_backtracks++;
              wpi = decision_tree.front();
            }
          }
          if (!wpi) no_test = true;
          goto again;  // if we want multiple patterns per fault
        } // if total_attempt_num > attempt_num
      }  // if check_test()
    } // again
  } // while (three conditions)

  /* clear everthing */
  for (wptr wptr_ele: decision_tree) {
    wptr_ele->flag &= ~ALL_ASSIGNED;
  }
  decision_tree.clear();
  
  current_backtracks = no_of_backtracks;
  unmark_propagate_tree(fault->node);
  
  if (find_test) {
    /* normally, we want one pattern per fault */
    if (total_attempt_num == 1) {
	  
      for (i = 0; i < ncktin; i++) {
        switch (cktin[i]->value) {
        case 0:
        case 1: break;
        case D: cktin[i]->value = 1; break;
        case B: cktin[i]->value = 0; break;
        case U: cktin[i]->value = rand()&01; break; // random fill U
        }
      }
      display_io();
    }
    else fprintf(stdout, "\n");  // do not random fill when multiple patterns per fault
    return(TRUE);
  }
  else if (no_test) {
    /*fprintf(stdout,"redundant fault...\n");*/
    return(FALSE);
  }
  else {
    /*fprintf(stdout,"test aborted due to backtrack limit...\n");*/
    return(MAYBE);
  }
}/* end of podem */


/* drive D or B to the faulty gate (aka. GUT) output
 * insert D or B into the circuit.
 * returns w (the faulty gate output) if GUT output is set to D or B successfully.
 * returns NULL if if faulty gate output still remains unknown  */
ATPG::wptr ATPG::fault_evaluate(const fptr fault) {
  int temp1;
  wptr w;

  if (fault->io == GO) { // if fault is on GUT gate output
    w = fault->node->owire.front(); // w is GUT output wire
    if (w->value == U) return(nullptr);
    if (fault->fault_type == STUCK0 && w->value == 1) w->value = D; // D means 1/0
    if (fault->fault_type == STUCK1 && w->value == 0) w->value = B; // B_bar 0/1
    return(w);
  }
  else { // if fault is GUT gate input
    w = fault->node->iwire[fault->index]; 
    if (w->value == U) return(nullptr);
    else {
      temp1 = w->value;
      if (fault->fault_type == STUCK0 && w->value == 1) w->value = D;
      if (fault->fault_type == STUCK1 && w->value == 0) w->value = B;
      if (fault->node->type == OUTPUT) return(nullptr);
      evaluate(fault->node);  // five-valued, evaluate one gate only, sim.c
      w->value = temp1;
	  /* if GUT gate output changed */
      if (fault->node->owire.front()->flag & CHANGED) {
	    fault->node->owire.front()->flag &= ~CHANGED; // stop GUT output change propagation 
	    return (fault->node->owire.front()); // returns the output wire of GUT
      }
      else return(nullptr); // faulty gate output does not change
    }
  }
}/* end of fault_evaluate */


/* iteratively foward implication
 * in a depth first search manner*/ 
void ATPG::forward_imply(const wptr w) {
  int i,nout;

  for (i = 0, nout = w->onode.size(); i < nout; i++) {
    if (w->onode[i]->type != OUTPUT) {
      evaluate(w->onode[i]);
      if (w->onode[i]->owire.front()->flag & CHANGED)
	    forward_imply(w->onode[i]->owire.front()); // go one level further
      w->onode[i]->owire.front()->flag &= ~CHANGED;
    }
  }
}/* end of forward_imply */


/* Fig 8 
 * this function determines objective_wire and objective_level. 
 * it returns the newly assigned PI if test is possible. 
 * it returns NULL if no test is possible. */
ATPG::wptr ATPG::test_possible(const fptr fault) {
  nptr n;
  wptr object_wire;
  int object_level;

  /* if the fault is not on primary output */
  if (fault->node->type != OUTPUT) {

    /* if the faulty gate (aka. gate under test, G.U.T.) output is not U,  Fig. 8.1 */ 
    if (fault->node->owire.front()->value ^ U) {

	  /* if GUT output is not D or D_bar, no test possible */
	  if (!((fault->node->owire.front()->value == B) ||
        (fault->node->owire.front()->value == D))) return(nullptr);

	  /* find the next gate n to propagate, Fig 8.5*/
	  if (!(n = find_propagate_gate(fault->node->owire.front()->level)))
	  return(nullptr);

	  /*determine objective level according to the type of n.   Fig 8.8*/ 
      switch(n->type) {
        case  AND:
        case  NOR: object_level = 1; break;
        case NAND:
        case   OR: object_level = 0; break;
        default:
          /*---- comment out due to error for C2670.sim ---------
          fprintf(stderr,
              "Internal Error(1st bp. in test_possible)!\n");
          exit(-1);
          -------------------------------------------------------*/
          return(nullptr);
      }
      /* object_wire is the gate n output. */
      object_wire = n->owire.front();
    }  // if faulty gate output is not U.   (fault->node->owire.front()->value ^ U) 

    else { // if faulty gate output is U

      /* if X path disappear, no test possible  */
      if (!(trace_unknown_path(fault->node->owire.front())))
        return(nullptr);

      /* if fault is on GUT otuput,  Fig 8.2*/
      if (fault->io) {
        /* objective_level is opposite to stuck fault  Fig 8.3 */ 
        if (fault->fault_type) object_level = 0;
        else object_level = 1;
      /* objective_wire is on faulty gate output */
        object_wire = fault->node->owire.front();
      }

      /* if fault is on GUT input, Fig 8.2*/ 
      else {
        /* if faulted input is not U  Fig 8.4 */
        if (fault->node->iwire[fault->index]->value  ^ U) {
          /* determine objective value according to GUT type. Fig 8.9*/
          switch (fault->node->type) {
            case  AND:
            case  NOR: object_level = 1; break;
            case NAND:
            case   OR: object_level = 0; break;
            default:
              /*---- comment out due to error for C2670.sim ---------
                 fprintf(stderr,
                    "Internal Error(2nd bp. in test_possible)!\n");
                 exit(-1);
              -------------------------------------------------------*/
              return(nullptr);
          }
          /*objective wire is GUT output. */
          object_wire = fault->node->owire.front();
        }  // if faulted input is not U

        else { // if faulted input is U
          /*objective level is opposite to stuck fault.  Fig 8.10*/
          if (fault->fault_type) object_level = 0;
          else object_level = 1;
          /* objective wire is faulty wire itself */
          object_wire = fault->node->iwire[fault->index];
        }
      }
    }
  } // if fault not on PO

  else { // else if fault on PO
    /* if faulty PO is still unknown */
    if (fault->node->iwire.front()->value == U) {
      /*objective level is opposite to the stuck fault */ 
      if (fault->fault_type) object_level = 0;
      else object_level = 1;
      /* objective wire is the faulty wire itself */
      object_wire = fault->node->iwire.front();
    }

    else {
      /*--- comment out due to error for C2670.sim ---
      fprintf(stderr,"Internal Error(1st bp. in test_possible)!\n");
      exit(-1);
	    */
      return(nullptr);
    }
  }// else if fault on PO

  /* find a pi to achieve the objective_level on objective_wire.
   * returns nullptr if no PI is found.  */ 
  return(find_pi_assignment(object_wire,object_level));
}/* end of test_possible */


/* backtrace to PI, assign a PI to achieve the objective.  Fig 9
 * returns the wire pointer to PI if succeed.
 * returns NULL if no such PI found. */
ATPG::wptr ATPG::find_pi_assignment(const wptr object_wire, const int& object_level) {
  wptr new_object_wire;
  int new_object_level;
  
  /* if PI, assign the same value as objective Fig 9.1, 9.2 */
  if (object_wire->flag & INPUT) {
    object_wire->value = object_level;
    return(object_wire);
  }

  /* if not PI, backtrace to PI  Fig 9.3, 9.4, 9.5*/
  else {
    switch(object_wire->inode.front()->type) {
      case   OR:
      case NAND:
        if (object_level) new_object_wire = find_easiest_control(object_wire->inode.front());  // decision gate
        else new_object_wire = find_hardest_control(object_wire->inode.front()); // imply gate
        break;
      case  NOR:
      case  AND:
		// TODO find the input wire  
    // Hint similar to OR and NAND but different polarity
    //--------------------------------- hole -------------------------------------------
        if (object_level) new_object_wire = find_hardest_control(object_wire->inode.front());
        else new_object_wire = find_easiest_control(object_wire->inode.front());
        break;
    //----------------------------------------------------------------------------------
		//  TODO 
      case  NOT:
      case  BUF:
        new_object_wire = object_wire->inode.front()->iwire.front();
        break;
    }

    switch (object_wire->inode.front()->type) {
      case  BUF:
      case  AND:
      case   OR: new_object_level = object_level; break;
      /* flip objective value  Fig 9.6 */
      case  NOT:
      case  NOR:
      case NAND: new_object_level = object_level ^ 1; break;
    }
    if (new_object_wire) return(find_pi_assignment(new_object_wire,new_object_level));
    else return(nullptr);
  }
}/* end of find_pi_assignment */


/* Fig 9.4 */
ATPG::wptr ATPG::find_hardest_control(const nptr n) {
  int i;
  /* because gate inputs are arranged in a increasing level order,
   * larger input index means harder to control */
  for (i = n->iwire.size() - 1; i >= 0; i--) {
    if (n->iwire[i]->value == U) return(n->iwire[i]);
  }
  return(nullptr);
}/* end of find_hardest_control */


/* Fig 9.5 */
ATPG::wptr ATPG::find_easiest_control(const nptr n) {
  int i, nin;
  // TODO  find the input with min level
  // Hint: similar to hardiest_control but increasing level order
  //--------------------------------- hole -------------------------------------------
  for (i = 0, nin = n->iwire.size(); i < nin; ++i) {
    if (n->iwire[i]->value == U) return n->iwire[i];
  }
  //----------------------------------------------------------------------------------
  //  TODO 
  return(nullptr);
}/* end of find_easiest_control */


/* Find the eastiest propagation gate.   Fig 8.5, Fig 8.6 
 * returns the next gate with D or B on inputs, U on output, nearest to PO
 * returns NULL if no such gate found. */
ATPG::nptr ATPG::find_propagate_gate(const int& level) {
  int i,j,nin;
  wptr w;

  /* check every wire in decreasing level order
   * so that wires nearer to PO is checked earlier. */
  for (i = sort_wlist.size() - 1; i >= 0; i--) {
    /* if reach the same level as the fault, then no propagation path exists */
    if (sort_wlist[i]->level == level) return(nullptr);
    /* gate outptu is U */
    /* a marked gate means it is on the path to PO */
    if ((sort_wlist[i]->value == U) &&
        (sort_wlist[i]->inode.front()->flag & MARKED)) { 
      /*  cehck all gate intputs */  
      for (j = 0, nin = sort_wlist[i]->inode.front()->iwire.size(); j < nin; j++) {
        w = sort_wlist[i]->inode.front()->iwire[j];
        /* if there is ont gate intput is D or B */
        if ((w->value == D) || (w->value == B)) {
          if (trace_unknown_path(sort_wlist[i])) // check X path  Fig 8.6
            return(sort_wlist[i]->inode.front()); // succeed.  returns this gate
          break;
        }
      }
    }
  }
}/* end of find_propagate_gate */


/* DFS search for X-path , Fig 8.6
 * returns TRUE if X pth exists
 * returns NULL if no X path exists*/
bool ATPG::trace_unknown_path(const wptr w) {
  int i,nout;
  wptr wtemp;

	//TODO search X-path
	//HINT if w is PO, return TRUE, if not, check all its fanout 
	//------------------------------------- hole ---------------------------------------
  // 1. If w is PO, return TRUE
  if (w->flag & OUTPUT) return true;
  // 2. If not, check all its fanout (DFS)
  #define DFS_MARKED 2048
  forward_list<wptr> wire_stack;
  forward_list<wptr> marked_wire_flist;
  auto __unmark_and_return = [&](const bool ret) -> bool {
    while(!marked_wire_flist.empty()) {
      marked_wire_flist.front()->flag &= ~DFS_MARKED;
      marked_wire_flist.pop_front();
    }
    return ret;
  };
  auto __push2stack_and_mark = [&](wptr w) -> void {
    wire_stack.push_front(w);         // push to stack
    w->flag |= DFS_MARKED;            // mark discovered wires
    marked_wire_flist.push_front(w);  // add to marked list
  };
  __push2stack_and_mark(w);
  while (!wire_stack.empty()) {
    wtemp = wire_stack.front(); // stack pop
    wire_stack.pop_front();     // stack pop
    for (i = 0, nout = wtemp->onode.size(); i < nout; ++i) {
      for (wptr wout : wtemp->onode[i]->owire) {
        if (!(wout->flag & DFS_MARKED) && (wout->value == U)) {
          if (wout->flag & OUTPUT)
            return __unmark_and_return(true);
          __push2stack_and_mark(wout);
        }
      }
    }
  }
	//----------------------------------------------------------------------------------
  return __unmark_and_return(false); // X-path disappear
  //TODO 
}/* end of trace_unknown_path */


/* Check if any D or D_bar reaches PO. Fig 7.4 */
bool ATPG::check_test(void) {
  int i,ncktout;
  bool is_test;

  is_test = false;
	//TODO check if any fault effect reach PO
	//HINT check every PO for their value
	//--------------------------------- hole -------------------------------------------
  for (i = 0, ncktout = cktout.size(); i < ncktout; ++i) {
    if ((cktout[i]->value == D) || (cktout[i]->value == B)) {
      is_test = true;
      break;
    }
  }
	//----------------------------------------------------------------------------------
  //TODO
  return is_test;
}/* end of check_test */

/* exhaustive search of all nodes on the path from n to PO */
void ATPG::mark_propagate_tree(const nptr n) {
  int i,j,wnout,nnout;

  if (n->flag & MARKED) return;
    n->flag |= MARKED; // MARKED means this node is on the path to PO 
  /* depth first search */
  for (i = 0, wnout = n->owire.size(); i < wnout; i++) {
    for (j = 0, nnout = n->owire[i]->onode.size(); j < nnout; j++) {
      mark_propagate_tree(n->owire[i]->onode[j]);
    }
  }
  return;
}/* end of mark_propagate_tree */

/* clear all the MARKS */ 
void ATPG::unmark_propagate_tree(const nptr n) {
  int i,j,wnout,nnout;

  if (n->flag & MARKED) {
    n->flag &= ~MARKED;
    for (i = 0, wnout = n->owire.size(); i < wnout; i++) {
      for (j = 0, nnout = n->owire[i]->onode.size(); j < nnout; j++) {
        unmark_propagate_tree(n->owire[i]->onode[j]);
      }
    }
  }
  return;
}/* end of unmark_propagate_tree */

/* set the initial objective.  
 * returns TRUE if we can backtrace to a PI to assign
 * returns CONFLICT if it is impossible to achieve or set the initial objective*/
int ATPG::set_uniquely_implied_value(const fptr fault) {
  wptr w;
  int pi_is_reach = FALSE;
  int i,nin;

  nin = fault->node->iwire.size();
  if (fault->io) w = fault->node->owire.front();  //  gate output fault, Fig.8.3
  else { // gate input fault.  Fig. 8.4 
    w = fault->node->iwire[fault->index]; 

    switch (fault->node->type) {
      case NOT:
      case BUF:
	    return(pi_is_reach);

	    /* assign all side inputs to non-controlling values */
      case AND:
      case NAND:
         for (i = 0; i < nin; i++) {
           if (fault->node->iwire[i] != w) {
             switch (backward_imply(fault->node->iwire[i],1)) {
                case TRUE: pi_is_reach = TRUE; break;
                case CONFLICT: return(CONFLICT); break;
                case FALSE: break;
             }
           }
         }
         break;

      case OR:
      case NOR:
         for (i = 0; i < nin; i++) {
           if (fault->node->iwire[i] != w) {
             switch (backward_imply(fault->node->iwire[i],0)) {
                case TRUE: pi_is_reach = TRUE; break;
                case CONFLICT: return(CONFLICT); break;
                case FALSE: break;
             }
           }
         }
         break;
    }
  } // else , gate input fault 
  
  
  /* fault excitation */
	//TODO fault excitation
	//HINT use backward_imply function to check if fault can excite or not
	//------------------------------------ hole ----------------------------------------
  const int opposite_stuck_value = (fault->fault_type == STUCK0 ? 1 : 0);
  pi_is_reach = (backward_imply(w, opposite_stuck_value) == CONFLICT ? CONFLICT : TRUE);
  //----------------------------------------------------------------------------------
  //TODO

  return(pi_is_reach);
}/* end of set_uniquely_implied_value */

/*do a backward implication of the objective: set current_wire to desired_logic_value
 *implication means a natural consequence of the desired objective. 
 *returns TRUE if the backward implication reaches at least one PI 
 *returns FALSE if the backward implication reaches no PI */
int ATPG::backward_imply(const wptr current_wire, const int& desired_logic_value) {
  int pi_is_reach = FALSE;
  int i, nin;

  nin = current_wire->inode.front()->iwire.size();
  if (current_wire->flag & INPUT) { // if PI
    if (current_wire->value != U &&  
      current_wire->value != desired_logic_value) { 
      return(CONFLICT); // conlict with previous assignment
    }
    current_wire->value = desired_logic_value; // assign PI to the objective value
    current_wire->flag |= CHANGED; 
    // CHANGED means the logic value on this wire has recently been changed
    return(TRUE);
  }
  else { // if not PI
    switch (current_wire->inode.front()->type) {
      /* assign NOT input opposite to its objective ouput */
      /* go backward iteratively.  depth first search */
      case NOT:
        switch (backward_imply(current_wire->inode.front()->iwire.front(), (desired_logic_value ^ 1))) {
          case TRUE: pi_is_reach = TRUE; break;
          case CONFLICT: return(CONFLICT); break;
          case FALSE: break;
        }
        break;

		/* if objective is NAND output=zero, then NAND inputs are all ones  
		 * keep doing this back implication iteratively  */
      case NAND:
        if (desired_logic_value == 0) {
          for (i = 0; i < nin; i++) {
            switch (backward_imply(current_wire->inode.front()->iwire[i],1)) {
              case TRUE: pi_is_reach = TRUE; break;
              case CONFLICT: return(CONFLICT); break;
              case FALSE: break;
            }
          }
        }
        break;

      case AND:
        if (desired_logic_value == 1) {
          for (i = 0; i < nin; i++) {
            switch (backward_imply(current_wire->inode.front()->iwire[i],1)) {
              case TRUE: pi_is_reach = TRUE; break;
              case CONFLICT: return(CONFLICT); break;
              case FALSE: break;
            }
          }
        }
        break;

      case OR:
        if (desired_logic_value == 0) {
          for (i = 0; i < nin; i++) {
            switch (backward_imply(current_wire->inode.front()->iwire[i],0)) {
              case TRUE: pi_is_reach = TRUE; break;
              case CONFLICT: return(CONFLICT); break;
              case FALSE: break;
            }
          }
        }
        break;

      case NOR:
        if (desired_logic_value == 1) {
          for (i = 0; i < nin; i++) {
            switch (backward_imply(current_wire->inode.front()->iwire[i],0)) {
              case TRUE: pi_is_reach = TRUE; break;
              case CONFLICT: return(CONFLICT); break;
              case FALSE: break;
            }
          }
        }
        break;

      case BUF:
        switch (backward_imply(current_wire->inode.front()->iwire.front(),desired_logic_value)) {
          case TRUE: pi_is_reach = TRUE; break;
          case CONFLICT: return(CONFLICT); break;
          case FALSE: break;
        }
        break;
    }
	
    return(pi_is_reach);
  }
}/* end of backward_imply */