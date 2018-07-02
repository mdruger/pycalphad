from pycalphad import variables as v


# There are two general types of reference states we want to consider:
# 1. Internal DOF and a phase is specified (calculate type)
# 2. A set of phases and equilibrium conditions is specified (equilibrium type)
class ReferenceState(object):
    """
    Specify a reference state to be used in calculate or equilbrium

    Smartly constructs the object such that it can calculate a reference point on a hyperplane
    """

    def __init__(self, phases, conditions, components=None):
        # TODO: fixme
        if True:
            self._type = 'calculate' # or 'equilibrium'

    def compute_hyperplane_point(self, conditions):
        # TODO: compute chemical potentials of internal DOF type?

        # TODO: calculate and return an equilibrium dataset with the right output for the current conditions, including broadcasted conditions
        # should have the same shape as some input Dataset from calculate or equilibrium
        pass

def construct_hyperplane(calculated_dataset, current_conditions, reference_states):

    # TODO: Determine if the number reference states can cover the entire space

    # TODO: construct a hyperplane and extend it over all space

    # TODO: shift the calculated values in the passed dataset (optional: create new suffix "R" outputs vs. change in place)
    pass
